#include "hlk_ld2402.h"

namespace esphome {
namespace hlk_ld2402 {

void HLKLD2402Component::setup() {
  ESP_LOGCONFIG(TAG, "Setting up HLK-LD2402...");
  
  if (!enter_config_mode_()) {
    ESP_LOGE(TAG, "Failed to enter config mode");
    return;
  }

  // Get firmware version
  std::vector<uint8_t> response;
  if (send_command_(CMD_GET_VERSION) && read_response_(response)) {
    if (response.size() >= 8) {
      size_t version_len = response[4] | (response[5] << 8);
      if (version_len + 6 <= response.size()) {
        firmware_version_ = std::string(response.begin() + 6, response.begin() + 6 + version_len);
        ESP_LOGCONFIG(TAG, "Firmware Version: %s", firmware_version_.c_str());
      }
    }
  }

  // Set max distance (convert meters to decimeters)
  uint32_t distance_dm = max_distance_ * 10;
  if (!set_parameter_(PARAM_MAX_DISTANCE, distance_dm)) {
    ESP_LOGE(TAG, "Failed to set max distance");
  }

  // Set timeout
  if (!set_parameter_(PARAM_TIMEOUT, timeout_)) {
    ESP_LOGE(TAG, "Failed to set timeout");
  }

  // Set to normal working mode
  if (!set_work_mode_(MODE_NORMAL)) {
    ESP_LOGE(TAG, "Failed to set normal mode");
  }

  // Save configuration
  if (!send_command_(CMD_SAVE_PARAMS)) {
    ESP_LOGE(TAG, "Failed to save parameters");
  }

  exit_config_mode_();
}

void HLKLD2402Component::loop() {
  while (available()) {
    uint8_t c;
    read_byte(&c);
    
    if (c == '\n') {
      // Process complete line
      if (!line_buffer_.empty()) {
        process_line_(line_buffer_);
        line_buffer_.clear();
      }
    } else if (c != '\r') {  // Skip \r
      line_buffer_ += (char)c;
    }
  }
}

void HLKLD2402Component::process_line_(const std::string &line) {
  ESP_LOGV(TAG, "Processing line: '%s'", line.c_str());
  
  if (line.compare(0, 9, "distance:") == 0) {
    // Extract distance value
    std::string distance_str = line.substr(9);
    ESP_LOGD(TAG, "Extracted distance string: '%s'", distance_str.c_str());
    
    char *end;
    float distance = strtof(distance_str.c_str(), &end);
    
    if (end != distance_str.c_str() && *end == '\0') {  // Successful conversion
      ESP_LOGD(TAG, "Parsed distance value: %.0f cm", distance);
      
      if (this->distance_sensor_ != nullptr) {
        ESP_LOGD(TAG, "Publishing distance: %.0f cm", distance);
        this->distance_sensor_->publish_state(distance);
      } else {
        ESP_LOGW(TAG, "Distance sensor not configured");
      }
      
      if (this->presence_binary_sensor_ != nullptr) {
        ESP_LOGD(TAG, "Publishing presence: TRUE");
        this->presence_binary_sensor_->publish_state(true);
      } else {
        ESP_LOGW(TAG, "Presence sensor not configured");
      }
    } else {
      ESP_LOGW(TAG, "Failed to parse distance value: '%s'", distance_str.c_str());
    }
  } else if (line == "OFF") {
    ESP_LOGD(TAG, "Received OFF signal");
    if (this->presence_binary_sensor_ != nullptr) {
      ESP_LOGD(TAG, "Publishing presence: FALSE");
      this->presence_binary_sensor_->publish_state(false);
    } else {
      ESP_LOGW(TAG, "Presence sensor not configured");
    }
  } else {
    ESP_LOGW(TAG, "Unknown line format: '%s'", line.c_str());
  }
}

void HLKLD2402Component::dump_config() {
  ESP_LOGCONFIG(TAG, "HLK-LD2402:");
  ESP_LOGCONFIG(TAG, "  Firmware Version: %s", firmware_version_.c_str());
  ESP_LOGCONFIG(TAG, "  Max Distance: %.1f m", max_distance_);
  ESP_LOGCONFIG(TAG, "  Timeout: %u s", timeout_);
}

bool HLKLD2402Component::send_command_(uint16_t command, const uint8_t *data, size_t len) {
  std::vector<uint8_t> frame;
  
  // Header
  frame.insert(frame.end(), FRAME_HEADER, FRAME_HEADER + 4);
  
  // Length (2 bytes)
  uint16_t total_len = 2 + len;  // command (2 bytes) + data length
  frame.push_back(total_len & 0xFF);
  frame.push_back((total_len >> 8) & 0xFF);
  
  // Command (2 bytes)
  frame.push_back(command & 0xFF);
  frame.push_back((command >> 8) & 0xFF);
  
  // Data (if any)
  if (data != nullptr && len > 0) {
    frame.insert(frame.end(), data, data + len);
  }
  
  // Footer
  frame.insert(frame.end(), FRAME_FOOTER, FRAME_FOOTER + 4);
  
  dump_hex_(frame.data(), frame.size(), "TX");
  write_array(frame.data(), frame.size());
  
  return true;
}

bool HLKLD2402Component::read_response_(std::vector<uint8_t> &response) {
  uint32_t start = millis();
  std::vector<uint8_t> buffer;
  uint8_t header_match = 0;
  uint8_t footer_match = 0;
  
  while ((millis() - start) < 1000) {  // 1 second timeout
    if (available()) {
      uint8_t c;
      read_byte(&c);
      
      // Look for header
      if (header_match < 4) {
        if (c == FRAME_HEADER[header_match]) {
          header_match++;
          buffer.push_back(c);
        } else {
          header_match = 0;
          buffer.clear();
        }
        continue;
      }
      
      buffer.push_back(c);
      
      // Look for footer
      if (c == FRAME_FOOTER[footer_match]) {
        footer_match++;
        if (footer_match == 4) {
          dump_hex_(buffer.data(), buffer.size(), "RX");
          
          // Extract the actual response data (remove header and footer)
          response.assign(buffer.begin() + 4, buffer.end() - 4);
          return true;
        }
      } else {
        footer_match = 0;
      }
    }
    yield();
  }
  
  ESP_LOGW(TAG, "Response timeout");
  return false;
}

void HLKLD2402Component::dump_hex_(const uint8_t *data, size_t len, const char* prefix) {
  char buf[128];
  size_t pos = 0;
  
  for (size_t i = 0; i < len && pos < sizeof(buf) - 3; i++) {
    pos += sprintf(buf + pos, "%02X ", data[i]);
  }
  
  ESP_LOGV(TAG, "%s: %s", prefix, buf);
}

bool HLKLD2402Component::enter_config_mode_() {
  if (config_mode_)
    return true;
    
  uint8_t data[] = {0x01, 0x00};  // Enable configuration
  if (!send_command_(CMD_ENABLE_CONFIG, data, sizeof(data)))
    return false;
    
  std::vector<uint8_t> response;
  if (!read_response_(response))
    return false;
    
  if (response.size() >= 2 && response[0] == 0x00 && response[1] == 0x00) {
    config_mode_ = true;
    return true;
  }
  
  return false;
}

bool HLKLD2402Component::exit_config_mode_() {
  if (!config_mode_)
    return true;
    
  if (!send_command_(CMD_DISABLE_CONFIG))
    return false;
    
  std::vector<uint8_t> response;
  if (!read_response_(response))
    return false;
    
  if (response.size() >= 2 && response[0] == 0x00 && response[1] == 0x00) {
    config_mode_ = false;
    return true;
  }
  
  return false;
}

bool HLKLD2402Component::set_parameter_(uint16_t param_id, uint32_t value) {
  uint8_t data[6];
  data[0] = param_id & 0xFF;
  data[1] = (param_id >> 8) & 0xFF;
  data[2] = value & 0xFF;
  data[3] = (value >> 8) & 0xFF;
  data[4] = (value >> 16) & 0xFF;
  data[5] = (value >> 24) & 0xFF;
  
  if (!send_command_(CMD_SET_PARAMS, data, sizeof(data)))
    return false;
    
  std::vector<uint8_t> response;
  if (!read_response_(response))
    return false;
    
  return (response.size() >= 2 && response[0] == 0x00 && response[1] == 0x00);
}

bool HLKLD2402Component::get_parameter_(uint16_t param_id, uint32_t &value) {
  uint8_t data[2];
  data[0] = param_id & 0xFF;
  data[1] = (param_id >> 8) & 0xFF;
  
  if (!send_command_(CMD_GET_PARAMS, data, sizeof(data)))
    return false;
    
  std::vector<uint8_t> response;
  if (!read_response_(response))
    return false;
    
  if (response.size() >= 6) {
    value = response[2] | (response[3] << 8) | (response[4] << 16) | (response[5] << 24);
    return true;
  }
  
  return false;
}

bool HLKLD2402Component::set_work_mode_(uint32_t mode) {
  uint8_t mode_data[6];
  mode_data[0] = 0x00;  // Command value
  mode_data[1] = 0x00;
  mode_data[2] = mode & 0xFF;
  mode_data[3] = (mode >> 8) & 0xFF;
  mode_data[4] = (mode >> 16) & 0xFF;
  mode_data[5] = (mode >> 24) & 0xFF;
  
  if (!send_command_(CMD_SET_MODE, mode_data, sizeof(mode_data))) {
    return false;
  }

  std::vector<uint8_t> response;
  if (!read_response_(response)) {
    return false;
  }

  return (response.size() >= 2 && response[0] == 0x00 && response[1] == 0x00);
}

void HLKLD2402Component::calibrate() {
  ESP_LOGI(TAG, "Starting calibration...");
  
  if (!enter_config_mode_()) {
    ESP_LOGE(TAG, "Failed to enter config mode");
    return;
  }
  
  // Default calibration parameters - 3.0 for all thresholds (multiplied by 10)
  uint8_t data[] = {
    0x1E, 0x00,  // Trigger threshold coefficient (3.0)
    0x1E, 0x00,  // Hold threshold coefficient (3.0)
    0x1E, 0x00   // Micromotion threshold coefficient (3.0)
  };
  
  if (send_command_(CMD_START_CALIBRATION, data, sizeof(data))) {
    ESP_LOGI(TAG, "Started calibration");
    
    // Poll calibration status until complete or timeout
    uint32_t start = millis();
    while ((millis() - start) < 30000) {  // 30 second timeout
      if (send_command_(CMD_GET_CALIBRATION_STATUS)) {
        std::vector<uint8_t> response;
        if (read_response_(response) && response.size() >= 4) {
          uint16_t progress = response[2] | (response[3] << 8);
          ESP_LOGI(TAG, "Calibration progress: %u%%", progress);
          if (progress == 100)
            break;
        }
      }
      delay(1000);  // Check every second
    }
  }
  
  exit_config_mode_();
}

void HLKLD2402Component::save_config() {
  ESP_LOGI(TAG, "Saving configuration...");
  
  if (!enter_config_mode_()) {
    ESP_LOGE(TAG, "Failed to enter config mode");
    return;
  }
    
  if (send_command_(CMD_SAVE_PARAMS)) {
    std::vector<uint8_t> response;
    if (read_response_(response) && response.size() >= 2 && response[0] == 0x00 && response[1] == 0x00) {
      ESP_LOGI(TAG, "Configuration saved");
    } else {
      ESP_LOGE(TAG, "Failed to save configuration");
    }
  } else {
    ESP_LOGE(TAG, "Failed to send save command");
  }
  
  exit_config_mode_();
}

void HLKLD2402Component::enable_auto_gain() {
  ESP_LOGI(TAG, "Enabling auto gain...");
  
  if (!enter_config_mode_()) {
    ESP_LOGE(TAG, "Failed to enter config mode");
    return;
  }
    
  if (send_command_(CMD_AUTO_GAIN)) {
    std::vector<uint8_t> response;
    if (read_response_(response) && response.size() >= 2 && response[0] == 0x00 && response[1] == 0x00) {
      ESP_LOGI(TAG, "Auto gain enabled");
      
      // Wait for completion response (0xF0 command)
      uint32_t start = millis();
      while ((millis() - start) < 5000) {  // 5 second timeout
        if (available()) {
          std::vector<uint8_t> completion;
          if (read_response_(completion) && 
              completion.size() >= 4 && 
              completion[0] == 0xF0 && 
              completion[1] == 0x00) {
            ESP_LOGI(TAG, "Auto gain completed successfully");
            break;
          }
        }
        delay(100);
      }
    } else {
      ESP_LOGE(TAG, "Failed to enable auto gain");
    }
  } else {
    ESP_LOGE(TAG, "Failed to send auto gain command");
  }
  
  exit_config_mode_();
}

}  // namespace hlk_ld2402
}  // namespace esphome