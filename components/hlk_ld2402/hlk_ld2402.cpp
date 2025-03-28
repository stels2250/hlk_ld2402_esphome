#include "hlk_ld2402.h"
#include "esphome/core/log.h"

namespace esphome {
namespace hlk_ld2402 {

static const char *const TAG = "hlk_ld2402";

void HLKLD2402Component::setup() {
  ESP_LOGCONFIG(TAG, "Setting up HLK-LD2402...");
  
  // Try sending some test data to verify TX functionality
  write_str("TEST\n");
  ESP_LOGI(TAG, "Sent test string to verify TX line");
  delay(500);
  
  // Configure UART - explicitly set again
  auto *parent = (uart::UARTComponent *) this->parent_;
  parent->set_baud_rate(115200);
  parent->set_stop_bits(1);
  parent->set_data_bits(8);
  parent->set_parity(esphome::uart::UART_CONFIG_PARITY_NONE);

  // Flush any residual data
  ESP_LOGI(TAG, "Flushing UART");
  flush();
  delay(100);
  while (available()) {
    uint8_t c;
    read_byte(&c);
    ESP_LOGD(TAG, "Flushed byte: 0x%02X", c);
  }
    
  ESP_LOGD(TAG, "UART configured. Starting initialization sequence...");

  // Try multiple times to enter config mode
  bool config_success = false;
  for (int i = 0; i < 3 && !config_success; i++) {
    ESP_LOGI(TAG, "Attempt %d to enter config mode", i+1);
    if (enter_config_mode_()) {
      config_success = true;
      break;
    }
    delay(500);
  }

  if (!config_success) {
    ESP_LOGE(TAG, "Failed to enter config mode after multiple attempts");
    
    // Alternative initialization - try direct engineering mode
    ESP_LOGI(TAG, "Trying direct mode change as fallback");
    set_work_mode_(MODE_ENGINEERING);
    delay(100);
    
    return;
  }

  // Enable auto gain first
  enable_auto_gain();
  
  // Check power interference
  check_power_interference();

  bool setup_success = false;  // Add this missing variable
  
  // Continue with normal setup
  do {
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

    // Set thresholds based on documented ranges
    if (!set_parameter_(PARAM_TRIGGER_THRESHOLD, db_to_threshold_(3.0))) {
      ESP_LOGE(TAG, "Failed to set trigger threshold");
      break;
    }

    if (!set_parameter_(PARAM_MICRO_THRESHOLD, db_to_threshold_(3.0))) {
      ESP_LOGE(TAG, "Failed to set micro threshold");
      break;
    }

    // Set to normal working mode
    ESP_LOGD(TAG, "Setting to normal work mode");
    if (!set_work_mode_(MODE_NORMAL)) {
      ESP_LOGE(TAG, "Failed to set normal mode");
      break;
    }

    // Save configuration
    save_config();

    setup_success = true;
  } while (0);

  exit_config_mode_();

  // Add debug log at the end of setup
  if (setup_success) {
    ESP_LOGI(TAG, "Setup completed successfully. Waiting for data from sensor...");
  } else {
    ESP_LOGE(TAG, "Setup failed. Sensor may not work properly.");
  }

  // At the end of setup
  ESP_LOGI(TAG, "Setup completed %s. Radar should now start sending data.",
           setup_success ? "successfully" : "with errors");
  
  // Try both modes - first engineering for debugging, then production
  ESP_LOGI(TAG, "Switching to engineering mode for debugging");
  set_work_mode_(MODE_ENGINEERING);
  delay(500);
  
  ESP_LOGI(TAG, "Switching to production mode");
  set_work_mode_(MODE_PRODUCTION);
  delay(500);
  
  ESP_LOGI(TAG, "Switching back to engineering mode for better debugging");
  set_work_mode_(MODE_ENGINEERING);
  delay(500);
  
  // Force another save just to be sure
  save_config();
}

void HLKLD2402Component::loop() {
  static uint32_t last_byte_time = 0;
  static const uint32_t TIMEOUT_MS = 100; // Reset buffer if no data for 100ms
  static uint32_t last_debug_time = 0;
  static uint8_t raw_buffer[16];
  static size_t raw_pos = 0;
  static uint32_t last_status_time = 0;
  static uint32_t byte_count = 0;
  static uint8_t last_bytes[16] = {0};
  static size_t last_byte_pos = 0;
  static uint32_t last_command_time = 0;
  static const uint32_t COMMAND_INTERVAL = 30000; // 30 seconds
  static uint32_t loop_count = 0;
  
  // Increment loop counter for diagnostics
  loop_count++;
  
  // Every 30 seconds, try a special command to wake up the module
  if (millis() - last_command_time > COMMAND_INTERVAL) {
    ESP_LOGI(TAG, "Loop has run %u times. Sending wake-up command...", loop_count);
    
    // First try direct engineering mode
    set_work_mode_(MODE_ENGINEERING);
    delay(100);
    
    // Then try turning on auto gain again
    enable_auto_gain();
    
    // Reset the timer
    last_command_time = millis();
    loop_count = 0;
  }
  
  // Add periodic debug message
  if (millis() - last_debug_time > 5000) {  // Every 5 seconds
    ESP_LOGD(TAG, "Waiting for data. Available bytes: %d", available());
    last_debug_time = millis();
  }
  
  // Every 10 seconds, report status
  if (millis() - last_status_time > 10000) {
    ESP_LOGI(TAG, "Status: received %u bytes in last 10 seconds", byte_count);
    if (byte_count > 0) {
      char hex_buf[50] = {0};
      char ascii_buf[20] = {0};
      for (int i = 0; i < 16 && i < byte_count; i++) {
        sprintf(hex_buf + (i*3), "%02X ", last_bytes[i]);
        sprintf(ascii_buf + i, "%c", (last_bytes[i] >= 32 && last_bytes[i] < 127) ? last_bytes[i] : '.');
      }
      ESP_LOGI(TAG, "Last bytes (hex): %s", hex_buf);
      ESP_LOGI(TAG, "Last bytes (ascii): %s", ascii_buf);
    }
    byte_count = 0;
    last_status_time = millis();
  }
  
  while (available()) {
    uint8_t c;
    read_byte(&c);
    last_byte_time = millis();
    byte_count++;
    
    // Record last bytes for diagnostics
    last_bytes[last_byte_pos] = c;
    last_byte_pos = (last_byte_pos + 1) % 16;
    
    // Debug: Collect and show raw data periodically
    raw_buffer[raw_pos++] = c;
    if (raw_pos >= sizeof(raw_buffer)) {
      ESP_LOGD(TAG, "Raw data received (hex): %02X %02X %02X %02X %02X %02X %02X %02X...", 
               raw_buffer[0], raw_buffer[1], raw_buffer[2], raw_buffer[3],
               raw_buffer[4], raw_buffer[5], raw_buffer[6], raw_buffer[7]);
      raw_pos = 0;
    }
    
    if (c == '\n') {
      // Process complete line
      if (!line_buffer_.empty()) {
        ESP_LOGI(TAG, "Received line: '%s'", line_buffer_.c_str());
        process_line_(line_buffer_);
        line_buffer_.clear();
      }
    } else if (c != '\r') {  // Skip \r
      if (line_buffer_.length() < 1024) {
        line_buffer_ += (char)c;
      } else {
        ESP_LOGW(TAG, "Line buffer overflow, clearing");
        line_buffer_.clear();
      }
    }
  }
  
  // Reset buffer if no data received for a while
  if (!line_buffer_.empty() && (millis() - last_byte_time > TIMEOUT_MS)) {
    line_buffer_.clear();
  }
}

void HLKLD2402Component::process_line_(const std::string &line) {
  ESP_LOGD(TAG, "Processing line: '%s'", line.c_str());
  
  if (line == "OFF") {
    ESP_LOGI(TAG, "No target detected");
    if (this->presence_binary_sensor_ != nullptr) {
      this->presence_binary_sensor_->publish_state(false);
    }
    if (this->micromovement_binary_sensor_ != nullptr) {
      this->micromovement_binary_sensor_->publish_state(false);
    }
    if (this->distance_sensor_ != nullptr) {
      this->distance_sensor_->publish_state(0);
    }
    return;
  }

  if (line.compare(0, 9, "distance:") == 0) {
    std::string distance_str = line.substr(9);
    size_t pos = distance_str.find(" m");
    if (pos != std::string::npos) {
      distance_str = distance_str.substr(0, pos);
    }
    
    char *end;
    float distance = strtof(distance_str.c_str(), &end);
    
    if (end != distance_str.c_str() && (*end == '\0' || *end == ' ')) {
      distance = distance * 100;  // Convert to cm
      
      if (this->distance_sensor_ != nullptr) {
        this->distance_sensor_->publish_state(distance);
      }
      
      // Update presence states based on documented ranges
      if (this->presence_binary_sensor_ != nullptr) {
        bool is_presence = distance <= (STATIC_RANGE * 100);
        this->presence_binary_sensor_->publish_state(is_presence);
      }
      
      if (this->micromovement_binary_sensor_ != nullptr) {
        bool is_micro = distance <= (MICROMOVEMENT_RANGE * 100);
        this->micromovement_binary_sensor_->publish_state(is_micro);
      }
    }
  }
}

void HLKLD2402Component::dump_config() {
  ESP_LOGCONFIG(TAG, "HLK-LD2402:");
  ESP_LOGCONFIG(TAG, "  Firmware Version: %s", firmware_version_.c_str());
  ESP_LOGCONFIG(TAG, "  Max Distance: %.1f m", max_distance_);
  ESP_LOGCONFIG(TAG, "  Timeout: %u s", timeout_);
}

bool HLKLD2402Component::write_frame_(const std::vector<uint8_t> &frame) {
  size_t written = 0;
  size_t tries = 0;
  while (written < frame.size() && tries++ < 3) {
    size_t to_write = frame.size() - written;
    write_array(&frame[written], to_write);  // write_array returns void
    written += to_write;
    if (written < frame.size()) {
      delay(5);
    }
  }
  return written == frame.size();
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
  
  // Log the frame we're sending for debugging
  char hex_buf[128] = {0};
  for (size_t i = 0; i < frame.size() && i < 40; i++) {
    sprintf(hex_buf + (i*3), "%02X ", frame[i]);
  }
  ESP_LOGI(TAG, "Sending command 0x%04X, frame: %s", command, hex_buf);
  
  return write_frame_(frame);
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
  
  return false;
}

// Also enhance dump_hex_ to show even when not in VERY_VERBOSE mode
void HLKLD2402Component::dump_hex_(const uint8_t *data, size_t len, const char* prefix) {
  char buf[128];
  size_t pos = 0;
  
  for (size_t i = 0; i < len && pos < sizeof(buf) - 3; i++) {
    pos += sprintf(buf + pos, "%02X ", data[i]);
  }
  
  ESP_LOGI(TAG, "%s: %s", prefix, buf);  // Changed to LOGI to always print
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
  ESP_LOGI(TAG, "Setting work mode to %u (0x%X)", mode, mode);
  
  // Use production mode from manual instead of MODE_NORMAL
  if (mode == MODE_NORMAL) {
    mode = MODE_PRODUCTION;
    ESP_LOGI(TAG, "Using production mode 0x%X", mode);
  }
    
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

  // After setting mode, try to read any data to clear buffers
  flush();
  delay(100);
  ESP_LOGI(TAG, "After mode change, available bytes: %d", available());
  while (available()) {
    uint8_t c;
    read_byte(&c);
    ESP_LOGV(TAG, "Received byte after mode change: 0x%02X", c);
  }

  if (response.size() >= 2 && response[0] == 0x00 && response[1] == 0x00) {
    ESP_LOGI(TAG, "Work mode set successfully");
    return true;
  }
  
  ESP_LOGE(TAG, "Failed to set work mode");
  return false;
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

void HLKLD2402Component::check_power_interference() {
  uint32_t value;
  if (get_parameter_(PARAM_POWER_INTERFERENCE, value)) {
    power_interference_detected_ = (value == 2);
    if (power_interference_detected_) {
      ESP_LOGW(TAG, "Power interference detected!");
    }
  }
}

uint32_t HLKLD2402Component::db_to_threshold_(float db_value) {
  return static_cast<uint32_t>(pow(10, db_value / 10));
}

float HLKLD2402Component::threshold_to_db_(uint32_t threshold) {
  return 10 * log10(threshold);
}

}  // namespace hlk_ld2402
}  // namespace esphome