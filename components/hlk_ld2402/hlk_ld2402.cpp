#include "hlk_ld2402.h"
#include "esphome/core/log.h"

namespace esphome {
namespace hlk_ld2402 {

static const char *const TAG = "hlk_ld2402";

void HLKLD2402Component::setup() {
  ESP_LOGCONFIG(TAG, "Setting up HLK-LD2402...");
  ESP_LOGD(TAG, "Max distance: %.1f m, Timeout: %u s", max_distance_, timeout_);
  
  bool setup_success = false;
  
  if (!enter_config_mode_()) {
    ESP_LOGE(TAG, "Failed to enter config mode");
    return;
  }

  do {
    ESP_LOGD(TAG, "Entered config mode successfully");
    
    // Get firmware version
    std::vector<uint8_t> response;
    if (send_command_(CMD_GET_VERSION) && read_response_(response)) {
      ESP_LOGV(TAG, "Got version response, size: %d", response.size());
      if (response.size() >= 8) {
        size_t version_len = response[4] | (response[5] << 8);
        if (version_len + 6 <= response.size()) {
          firmware_version_ = std::string(response.begin() + 6, response.begin() + 6 + version_len);
          ESP_LOGCONFIG(TAG, "Firmware Version: %s", firmware_version_.c_str());
        }
      }
    } else {
      ESP_LOGE(TAG, "Failed to get firmware version");
      break;
    }

    // Set max distance (convert meters to decimeters)
    uint32_t distance_dm = max_distance_ * 10;
    ESP_LOGD(TAG, "Setting max distance to %u dm", distance_dm);
    if (!set_parameter_(PARAM_MAX_DISTANCE, distance_dm)) {
      ESP_LOGE(TAG, "Failed to set max distance");
      break;
    }

    // Set timeout
    ESP_LOGD(TAG, "Setting timeout to %u s", timeout_);
    if (!set_parameter_(PARAM_TIMEOUT, timeout_)) {
      ESP_LOGE(TAG, "Failed to set timeout");
      break;
    }

    // Set to normal working mode
    ESP_LOGD(TAG, "Setting to normal work mode");
    if (!set_work_mode_(MODE_NORMAL)) {
      ESP_LOGE(TAG, "Failed to set normal mode");
      break;
    }

    // Save configuration
    ESP_LOGD(TAG, "Saving configuration");
    if (!send_command_(CMD_SAVE_PARAMS)) {
      ESP_LOGE(TAG, "Failed to save parameters");
      break;
    }

    setup_success = true;
  } while (0);

  // Always try to exit config mode
  if (!exit_config_mode_()) {
    ESP_LOGE(TAG, "Failed to exit config mode");
  }

  if (setup_success) {
    ESP_LOGI(TAG, "Setup completed successfully");
  } else {
    ESP_LOGE(TAG, "Setup failed");
  }
}

void HLKLD2402Component::loop() {
  static uint32_t last_byte_time = 0;
  static const uint32_t TIMEOUT_MS = 100; // Reset buffer if no data for 100ms
  
  while (available()) {
    uint8_t c;
    read_byte(&c);
    last_byte_time = millis();
    
    // Log bytes for debugging
    if (isprint(c)) {
      ESP_LOGV(TAG, "Received byte: 0x%02X ('%c')", c, c);
    } else {
      ESP_LOGV(TAG, "Received byte: 0x%02X", c);
    }
    
    if (c == '\n') {
      // Process complete line
      if (!line_buffer_.empty()) {
        ESP_LOGD(TAG, "Complete line received: '%s'", line_buffer_.c_str());
        process_line_(line_buffer_);
        line_buffer_.clear();
      }
    } else if (c != '\r') {  // Skip \r
      if (line_buffer_.length() < 1024) {
        line_buffer_ += (char)c;
      } else {
        ESP_LOGW(TAG, "Buffer overflow, clearing");
        line_buffer_.clear();
      }
    }
  }
  
  // Reset buffer if no data received for a while
  if (!line_buffer_.empty() && (millis() - last_byte_time > TIMEOUT_MS)) {
    ESP_LOGW(TAG, "Data timeout, clearing buffer: '%s'", line_buffer_.c_str());
    line_buffer_.clear();
  }
}

void HLKLD2402Component::process_line_(const std::string &line) {
  ESP_LOGD(TAG, "Processing line: '%s'", line.c_str());
  
  if (line.compare(0, 9, "distance:") == 0) {
    std::string distance_str = line.substr(9);
    // Remove trailing " m" if present
    size_t pos = distance_str.find(" m");
    if (pos != std::string::npos) {
      distance_str = distance_str.substr(0, pos);
    }
    
    ESP_LOGV(TAG, "Parsing distance value: '%s'", distance_str.c_str());
    
    char *end;
    float distance = strtof(distance_str.c_str(), &end);
    
    if (end != distance_str.c_str() && (*end == '\0' || *end == ' ')) {
      // Convert meters to centimeters for ESPHome
      distance = distance * 100;  // Convert to cm
      ESP_LOGI(TAG, "Detected distance: %.2f cm", distance);
      
      if (this->distance_sensor_ != nullptr) {
        this->distance_sensor_->publish_state(distance);
        ESP_LOGI(TAG, "Published distance: %.2f cm", distance);
      }
      
      if (this->presence_binary_sensor_ != nullptr) {
        this->presence_binary_sensor_->publish_state(true);
        ESP_LOGI(TAG, "Published presence: TRUE");
      }
    } else {
      ESP_LOGW(TAG, "Failed to parse distance value: '%s'", distance_str.c_str());
    }
  } else if (line == "OFF") {
    ESP_LOGI(TAG, "Received OFF signal - no target detected");
    if (this->presence_binary_sensor_ != nullptr) {
      this->presence_binary_sensor_->publish_state(false);
      ESP_LOGI(TAG, "Published presence: FALSE");
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
  
  // Length (2 bytes) - command (2 bytes) + data length
  uint16_t total_len = 2 + len;  // 2 for command, plus any additional data
  frame.push_back(total_len & 0xFF);
  frame.push_back((total_len >> 8) & 0xFF);
  
  // Command (2 bytes, little endian)
  frame.push_back(command & 0xFF);
  frame.push_back((command >> 8) & 0xFF);
  
  // Data (if any)
  if (data != nullptr && len > 0) {
    frame.insert(frame.end(), data, data + len);
  }
  
  // Footer
  frame.insert(frame.end(), FRAME_FOOTER, FRAME_FOOTER + 4);
  
  ESP_LOGV(TAG, "Sending command 0x%04X with %d data bytes", command, len);
  dump_hex_(frame.data(), frame.size(), "TX");
  
  return write_array(frame.data(), frame.size());
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
  if (!buffer.empty()) {
    dump_hex_(buffer.data(), buffer.size(), "Incomplete RX");
  }
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
    
  ESP_LOGD(TAG, "Entering config mode...");
  
  // Just send the command with no data
  if (!send_command_(CMD_ENABLE_CONFIG)) {
    ESP_LOGE(TAG, "Failed to send config mode command");
    return false;
  }
    
  std::vector<uint8_t> response;
  if (!read_response_(response)) {
    ESP_LOGE(TAG, "No response to config mode command");
    return false;
  }
    
  // The response should be: FF 01 00 00 02 00 20 00
  if (response.size() >= 8) {
    // Check if response indicates success (00 00 after FF 01)
    if (response[0] == 0xFF && response[1] == 0x01 && 
        response[2] == 0x00 && response[3] == 0x00) {
      config_mode_ = true;
      ESP_LOGI(TAG, "Successfully entered config mode");
      return true;
    }
  }
  
  ESP_LOGE(TAG, "Invalid response to config mode command");
  return false;
}

bool HLKLD2402Component::exit_config_mode_() {
  if (!config_mode_)
    return true;
    
  ESP_LOGD(TAG, "Exiting config mode...");
  
  if (!send_command_(CMD_DISABLE_CONFIG)) {
    ESP_LOGE(TAG, "Failed to send exit config mode command");
    return false;
  }
    
  std::vector<uint8_t> response;
  if (!read_response_(response)) {
    ESP_LOGE(TAG, "No response to exit config mode command");
    return false;
  }
    
  if (response.size() >= 2 && response[0] == 0x00 && response[1] == 0x00) {
    config_mode_ = false;
    ESP_LOGI(TAG, "Successfully exited config mode");
    return true;
  }
  
  ESP_LOGE(TAG, "Invalid response to exit config mode command");
  return false;
}

bool HLKLD2402Component::set_parameter_(uint16_t param_id, uint32_t value) {
  ESP_LOGD(TAG, "Setting parameter 0x%04X to %u", param_id, value);
  
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
  ESP_LOGD(TAG, "Getting parameter 0x%04X", param_id);
  
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
    ESP_LOGD(TAG, "Parameter 0x%04X value: %u", param_id, value);
    return true;
  }
  
  return false;
}

bool HLKLD2402Component::set_work_mode_(uint32_t mode) {
  ESP_LOGD(TAG, "Setting work mode to %u", mode);
  
  uint8_t mode_data[6];
  mode_data[0] = 0x00;
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
  
  uint8_t data[] = {
    0x1E, 0x00,  // Trigger threshold coefficient (3.0)
    0x1E, 0x00,  // Hold threshold coefficient (3.0)
    0x1E, 0x00   // Micromotion threshold coefficient (3.0)
  };
  
  if (send_command_(CMD_START_CALIBRATION, data, sizeof(data))) {
    ESP_LOGI(TAG, "Started calibration");
    
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
      delay(1000);
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
      ESP_LOGI(TAG, "Configuration saved successfully");
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