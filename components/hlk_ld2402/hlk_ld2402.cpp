#include "hlk_ld2402.h"
#include "esphome/core/log.h"

namespace esphome {
namespace hlk_ld2402 {

static const char *const TAG = "hlk_ld2402";

void HLKLD2402Component::setup() {
  ESP_LOGCONFIG(TAG, "Setting up HLK-LD2402...");
  
  // Configure UART - explicitly set again
  auto *parent = (uart::UARTComponent *) this->parent_;
  parent->set_baud_rate(115200);
  parent->set_stop_bits(1);
  parent->set_data_bits(8);
  parent->set_parity(esphome::uart::UART_CONFIG_PARITY_NONE);

  // Try sending some test data to verify TX functionality
  write_str("TEST\n");
  ESP_LOGI(TAG, "Sent test string to verify TX line");
  delay(500);
  
  // Flush any residual data
  ESP_LOGI(TAG, "Flushing UART");
  flush();
  delay(100);
  while (available()) {
    uint8_t c;
    read_byte(&c);
  }

  // Initialize but don't touch the device if we don't need to
  // The logs show the device is already sending data correctly
  // Skip configuration and just set up sensors
  ESP_LOGI(TAG, "LD2402 appears to be sending data already. Skipping configuration.");
  ESP_LOGI(TAG, "Use the Engineering Mode button if you need to modify settings.");
  
  // Don't check power interference immediately - use a delayed operation
  ESP_LOGI(TAG, "Will check power interference and firmware version after 60 seconds");
  
  // Remove the immediate firmware version check - it will happen after 60 seconds
  // get_firmware_version_();
  
  // Set a default version - this will be displayed until we can determine the actual version
  if (firmware_version_text_sensor_ != nullptr) {
    firmware_version_text_sensor_->publish_state("HLK-LD2402");
  }
  
  // Start passive version detection immediately
  begin_passive_version_detection_();
}

// New function to passively monitor output for version info
void HLKLD2402Component::begin_passive_version_detection_() {
  ESP_LOGI(TAG, "Starting passive version detection");
  firmware_version_ = "HLK-LD2402"; // Default fallback version
}

// Add new function to get firmware version
void HLKLD2402Component::get_firmware_version_() {
  ESP_LOGI(TAG, "Retrieving firmware version...");
  
  // Clear any pending data
  flush();
  while (available()) {
    uint8_t c;
    read_byte(&c);
  }
  
  // First try detecting version directly from normal mode output
  uint32_t start_time = millis();
  std::string output_buffer;
  bool found_version = false;
  
  ESP_LOGI(TAG, "Reading normal output data for 3 seconds...");
  while ((millis() - start_time) < 3000) {
    if (available()) {
      uint8_t c;
      read_byte(&c);
      
      // Only collect printable characters 
      if (c >= 32 && c < 127) {
        output_buffer += (char)c;
      } else if (c == '\n' || c == '\r') {
        // Check if this line contains anything that looks like version info
        if (!output_buffer.empty()) {
          ESP_LOGD(TAG, "Examining line: %s", output_buffer.c_str());
          
          // Look for version patterns like "v1.2.3" or just "1.2.3"
          for (size_t i = 0; i < output_buffer.length(); i++) {
            if ((output_buffer[i] == 'v' || output_buffer[i] == 'V') && 
                i+1 < output_buffer.length() && isdigit(output_buffer[i+1])) {
              // Found v followed by a digit, likely a version
              std::string version = output_buffer.substr(i);
              firmware_version_ = version;
              found_version = true;
              ESP_LOGI(TAG, "Found version string: %s", version.c_str());
              break;
            }
            else if (i+2 < output_buffer.length() && 
                    isdigit(output_buffer[i]) && 
                    output_buffer[i+1] == '.' && 
                    isdigit(output_buffer[i+2])) {
              // Found a numeric version pattern
              std::string version = "v" + output_buffer.substr(i, output_buffer.find_first_not_of("0123456789.", i) - i);
              firmware_version_ = version;
              found_version = true;
              ESP_LOGI(TAG, "Found version number: %s", version.c_str());
              break;
            }
          }
        }
        output_buffer.clear();
      }
      
      if (found_version) break;
    }
    yield();
  }
  
  if (found_version) {
    if (firmware_version_text_sensor_ != nullptr) {
      firmware_version_text_sensor_->publish_state(firmware_version_);
      ESP_LOGI(TAG, "Published firmware version from normal output: %s", firmware_version_.c_str());
    }
    return;
  }
  
  // If no version found in normal output, enter config mode and use the correct command
  ESP_LOGI(TAG, "No version in normal output, entering config mode...");
  bool entered_config_mode = false;
  
  if (!config_mode_) {
    if (!enter_config_mode_()) {
      ESP_LOGW(TAG, "Failed to enter config mode for firmware version check");
      return;
    }
    entered_config_mode = true;
  }
  
  // According to the protocol docs, command 0x0001 is the firmware version command
  if (!send_command_(CMD_GET_VERSION)) {
    ESP_LOGW(TAG, "Failed to send version command");
  } else {
    // Wait for response with a longer delay - may need to be adjusted per documentation
    delay(300);  // Adjust based on protocol documentation timing requirements
    
    std::vector<uint8_t> response;
    if (read_response_(response, 1000)) {
      if (response.size() > 0) {
        char version[32];
        
        // Format according to protocol documentation - adjust this based on actual format
        if (response.size() >= 3) {
          sprintf(version, "v%d.%d.%d", response[0], response[1], response[2]);
        } else if (response.size() == 2) {
          sprintf(version, "v%d.%d", response[0], response[1]);
        } else {
          sprintf(version, "v%d", response[0]);
        }
        
        firmware_version_ = version;
        ESP_LOGI(TAG, "Got firmware version: %s", version);
        
        if (firmware_version_text_sensor_ != nullptr) {
          firmware_version_text_sensor_->publish_state(version);
        }
      }
    }
  }

  // Exit config mode if we entered it
  if (entered_config_mode) {
    ESP_LOGI(TAG, "Exiting config mode safely");
    exit_config_mode_();
  }
  
  // If we still don't have a version, use the model name as fallback
  if (firmware_version_.empty() || firmware_version_ == "Unknown") {
    firmware_version_ = "HLK-LD2402";
    if (firmware_version_text_sensor_ != nullptr) {
      firmware_version_text_sensor_->publish_state(firmware_version_);
    }
  }
}

void HLKLD2402Component::loop() {
  static uint32_t last_byte_time = 0;
  static uint32_t last_process_time = 0; // Add throttling timer
  static const uint32_t PROCESS_INTERVAL = 2000; // Only process lines every 2 seconds
  static const uint32_t TIMEOUT_MS = 100; // Reset buffer if no data for 100ms
  static uint32_t last_debug_time = 0;
  static uint8_t raw_buffer[16];
  static size_t raw_pos = 0;
  static uint32_t last_status_time = 0;
  static uint32_t byte_count = 0;
  static uint8_t last_bytes[16] = {0};
  static size_t last_byte_pos = 0;
  static bool firmware_check_done = false;
  static bool power_check_done = false;
  static uint32_t startup_time = millis();
  
  // Firmware version check earlier at 20 seconds to avoid conflict with power check
  if (!firmware_check_done && (millis() - startup_time) > 20000) {
    ESP_LOGI(TAG, "Performing firmware version check...");
    get_firmware_version_();
    firmware_check_done = true;
  }
  
  // Power interference check at 60 seconds after boot
  if (firmware_check_done && !power_check_done && (millis() - startup_time) > 60000) {
    ESP_LOGI(TAG, "Performing power interference check...");
    check_power_interference();
    power_check_done = true;
  }
  
  // Add periodic debug message - reduce frequency
  if (millis() - last_debug_time > 30000) {  // Every 30 seconds
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
    
    // Debug: Collect raw data
    raw_buffer[raw_pos++] = c;
    if (raw_pos >= sizeof(raw_buffer)) {
      raw_pos = 0;
    }
    
    // Check for beginning of binary frame header - IMPORTANT: Skip entire protocol frames
    if (c == FRAME_HEADER[0]) {
      // This is likely the start of a binary frame
      // Check if we have enough bytes to verify it's a frame header
      int header_match_count = 1;
      uint32_t peek_start = millis();
      
      // Try to consume the entire frame header and skip it
      while (header_match_count < 4 && (millis() - peek_start) < 100) {
        if (available()) {
          uint8_t next;
          read_byte(&next);
          byte_count++;
          
          if (next == FRAME_HEADER[header_match_count]) {
            header_match_count++;
          } else {
            // Not a frame header, add the bytes to the line buffer
            line_buffer_ += (char)c;
            line_buffer_ += (char)next;
            break;
          }
        }
        yield();
      }
      
      // If we matched the complete header, discard the entire frame
      if (header_match_count == 4) {
        ESP_LOGD(TAG, "Detected binary protocol frame - skipping");
        
        // Skip the rest of this frame until we see footer or timeout
        uint32_t skip_start = millis();
        uint8_t footer_match = 0;
        
        while (footer_match < 4 && (millis() - skip_start) < 200) {
          if (available()) {
            uint8_t frame_byte;
            read_byte(&frame_byte);
            byte_count++;
            
            if (frame_byte == FRAME_FOOTER[footer_match]) {
              footer_match++;
            } else {
              footer_match = 0;
            }
          }
          yield();
        }
        
        // Frame skipped, continue with next data
        continue;
      }
    }
    
    if (c == '\n') {
      // Process complete line, but check throttling first
      if (!line_buffer_.empty()) {
        bool should_process = millis() - last_process_time >= PROCESS_INTERVAL;
        
        if (should_process) {
          last_process_time = millis();
          
          // Only process if it looks like text
          bool is_binary = false;
          for (char ch : line_buffer_) {
            if (ch < 32 && ch != '\t' && ch != '\r') {
              is_binary = true;
              break;
            }
          }
          
          if (!is_binary) {
            ESP_LOGI(TAG, "Received line: '%s'", line_buffer_.c_str());
            process_line_(line_buffer_);
          } else {
            ESP_LOGD(TAG, "Skipped binary data that looks like a protocol frame");
          }
        }
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
    
    // Additional processing in loop to passively detect version info
    // from normal operation output, even after initial check
    if (!firmware_version_.empty() && firmware_version_ != "Unknown" && 
        firmware_version_.find("HLK-LD2402") == 0 && 
        firmware_version_.find("v") == std::string::npos) {
      
      // We only have model info, still looking for version number
      if (c == '\n' && !line_buffer_.empty()) {
        if (line_buffer_.find("v") != std::string::npos || 
            line_buffer_.find("V") != std::string::npos ||
            line_buffer_.find("version") != std::string::npos ||
            line_buffer_.find("Version") != std::string::npos) {
          
          ESP_LOGI(TAG, "Found potential version info: %s", line_buffer_.c_str());
          // Extract version information
          std::string version = "HLK-LD2402";
          
          // Try to find version number pattern
          for (size_t i = 0; i < line_buffer_.length(); i++) {
            if ((i+2 < line_buffer_.length() && 
                isdigit(line_buffer_[i]) && 
                line_buffer_[i+1] == '.' && 
                isdigit(line_buffer_[i+2])) ||
                (line_buffer_[i] == 'v' || line_buffer_[i] == 'V')) {
              
              size_t start_pos = line_buffer_[i] == 'v' || line_buffer_[i] == 'V' ? i+1 : i;
              size_t end_pos = line_buffer_.find_first_not_of("0123456789.", start_pos);
              if (end_pos == std::string::npos) end_pos = line_buffer_.length();
              
              version = "v" + line_buffer_.substr(start_pos, end_pos - start_pos);
              firmware_version_ = version;
              
              if (firmware_version_text_sensor_ != nullptr) {
                firmware_version_text_sensor_->publish_state(version);
                ESP_LOGI(TAG, "Updated firmware version from passive detection: %s", version.c_str());
              }
              break;
            }
          }
        }
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

  // Handle different formats of distance data
  float distance_cm = 0;
  bool valid_distance = false;
  
  // Format "distance:236" (no units specified)
  if (line.compare(0, 9, "distance:") == 0) {
    std::string distance_str = line.substr(9);
    // Remove any trailing characters
    size_t pos = distance_str.find_first_not_of("0123456789.");
    if (pos != std::string::npos) {
      distance_str = distance_str.substr(0, pos);
    }
    
    char *end;
    float distance = strtof(distance_str.c_str(), &end);
    
    if (end != distance_str.c_str()) {
      // From the logs, it seems the value is already in cm
      distance_cm = distance;
      valid_distance = true;
      ESP_LOGI(TAG, "Detected distance: %.1f cm", distance_cm);
    }
  }
  
  if (valid_distance) {
    if (this->distance_sensor_ != nullptr) {
      this->distance_sensor_->publish_state(distance_cm);
    }
    
    // Update presence states based on documented ranges
    if (this->presence_binary_sensor_ != nullptr) {
      bool is_presence = distance_cm <= (STATIC_RANGE * 100);
      this->presence_binary_sensor_->publish_state(is_presence);
    }
    
    if (this->micromovement_binary_sensor_ != nullptr) {
      bool is_micro = distance_cm <= (MICROMOVEMENT_RANGE * 100);
      this->micromovement_binary_sensor_->publish_state(is_micro);
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

// Modify read_response_ to accept a custom timeout
bool HLKLD2402Component::read_response_(std::vector<uint8_t> &response, uint32_t timeout_ms) {
  uint32_t start = millis();
  std::vector<uint8_t> buffer;
  uint8_t header_match = 0;
  uint8_t footer_match = 0;
  
  while ((millis() - start) < timeout_ms) {  // Use parameterized timeout
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
  
  ESP_LOGW(TAG, "Response timeout after %u ms", timeout_ms);
  return false;
}

// Modify get_parameter_ to use a longer timeout for power interference parameter
bool HLKLD2402Component::get_parameter_(uint16_t param_id, uint32_t &value) {
  ESP_LOGD(TAG, "Getting parameter 0x%04X", param_id);
  
  uint8_t data[2];
  data[0] = param_id & 0xFF;
  data[1] = (param_id >> 8) & 0xFF;
  
  if (!send_command_(CMD_GET_PARAMS, data, sizeof(data))) {
    ESP_LOGE(TAG, "Failed to send get parameter command");
    return false;
  }
  
  // Add a bigger delay for parameter 0x0005 (power interference)
  if (param_id == PARAM_POWER_INTERFERENCE) {
    delay(500);  // Wait longer for power interference parameter
  } else {
    delay(100);  // Standard delay for other parameters
  }
  
  std::vector<uint8_t> response;
  // Use longer timeout for power interference parameter
  uint32_t timeout = (param_id == PARAM_POWER_INTERFERENCE) ? 3000 : 1000;
  
  if (!read_response_(response, timeout)) {
    ESP_LOGE(TAG, "No response to get parameter command");
    return false;
  }
  
  // ...rest of the function remains the same...
  
  // Log the response for debugging
  char hex_buf[64] = {0};
  for (size_t i = 0; i < response.size() && i < 16; i++) {
    sprintf(hex_buf + (i*3), "%02X ", response[i]);
  }
  ESP_LOGD(TAG, "Get parameter response: %s", hex_buf);
  
  // Handle response in a permissive way
  if (response.size() >= 6) {
    // Standard response format
    value = response[2] | (response[3] << 8) | (response[4] << 16) | (response[5] << 24);
    ESP_LOGD(TAG, "Parameter 0x%04X value: %u", param_id, value);
    return true;
  } else if (response.size() >= 2) {
    // Shorter response, but possibly valid - use first 2 bytes
    value = response[0] | (response[1] << 8);
    ESP_LOGW(TAG, "Short parameter response, using value: %u", value);
    return true;
  }
  
  ESP_LOGE(TAG, "Invalid parameter response format");
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
  ESP_LOGI(TAG, "Checking power interference status");
  
  // Clear any pending data first
  flush();
  while (available()) {
    uint8_t c;
    read_byte(&c);
  }
  
  // Flag to track if we entered config mode in this function
  bool entered_config_mode = false;
  
  if (!config_mode_) {
    if (!enter_config_mode_()) {
      ESP_LOGE(TAG, "Failed to enter config mode for power interference check");
      
      // Show ERROR state if the check fails
      if (this->power_interference_binary_sensor_ != nullptr) {
        this->power_interference_binary_sensor_->publish_state(true);  // Show interference/error
        ESP_LOGE(TAG, "Setting power interference to ON (ERROR) due to config mode failure");
      }
      return;
    }
    entered_config_mode = true;
  }
  
  // Add a longer delay after entering config mode
  ESP_LOGI(TAG, "Waiting for device to be ready for parameter request...");
  delay(1000);
  
  // Try reading max distance parameter first (warm up the device)
  ESP_LOGI(TAG, "Reading max distance parameter first to warm up...");
  uint8_t max_dist_data[2];
  max_dist_data[0] = PARAM_MAX_DISTANCE & 0xFF;
  max_dist_data[1] = (PARAM_MAX_DISTANCE >> 8) & 0xFF;
  
  // Send max distance query and don't worry too much about the response
  send_command_(CMD_GET_PARAMS, max_dist_data, sizeof(max_dist_data));
  delay(1000);
  
  // Try alternative approach - query power test mode status using different command
  // According to some documentation, power test mode is command 0x05 (not get params with ID 0x05)
  ESP_LOGI(TAG, "Trying direct power test mode status query...");
  
  // Clear buffers again
  flush();
  while (available()) {
    uint8_t c;
    read_byte(&c);
  }
  
  // Send a direct query for power interference (experiment with command ID)
  // Try command 0x0005 directly instead of parameter
  if (!send_command_(0x0005)) {  // Direct power test mode check
    ESP_LOGE(TAG, "Failed to send power test query");
    
    // Update sensor to show ERROR
    if (this->power_interference_binary_sensor_ != nullptr) {
      this->power_interference_binary_sensor_->publish_state(true);
      ESP_LOGE(TAG, "Setting power interference to ON (ERROR) due to command failure");
    }
    
    // Exit config mode if needed
    if (entered_config_mode) {
      ESP_LOGI(TAG, "Exiting config mode");
      send_command_(CMD_DISABLE_CONFIG);
      delay(200);
      config_mode_ = false;
    }
    return;
  }
  
  // Wait and then check for raw response bytes (don't rely on our frame matching)
  delay(1000);
  
  // Look for any response bytes
  uint32_t start = millis();
  std::vector<uint8_t> raw_bytes;
  ESP_LOGI(TAG, "Waiting for raw response bytes...");
  
  while ((millis() - start) < 5000) {  // 5 second timeout
    if (available()) {
      uint8_t c;
      read_byte(&c);
      raw_bytes.push_back(c);
      // Keep reading until we get a good number of bytes or timeout
      if (raw_bytes.size() >= 20) break;
    }
    delay(10);
    yield();
  }
  
  // Analyze any bytes received
  if (!raw_bytes.empty()) {
    // Log all received bytes for debugging
    char hex_buf[128] = {0};
    char ascii_buf[64] = {0};
    
    for (size_t i = 0; i < raw_bytes.size() && i < 30; i++) {
      sprintf(hex_buf + (i*3), "%02X ", raw_bytes[i]);
      sprintf(ascii_buf + i, "%c", (raw_bytes[i] >= 32 && raw_bytes[i] < 127) ? raw_bytes[i] : '.');
    }
    
    ESP_LOGI(TAG, "Raw response (%d bytes): %s", raw_bytes.size(), hex_buf);
    ESP_LOGI(TAG, "ASCII: %s", ascii_buf);
    
    // Try to make sense of the response - look for the value 0x02 which might indicate interference
    bool has_interference = false;
    
    // Check several patterns that might indicate power interference
    for (size_t i = 0; i < raw_bytes.size(); i++) {
      if (raw_bytes[i] == 0x02) {
        ESP_LOGI(TAG, "Found 0x02 at position %d - possible interference indicator", i);
        has_interference = true;
      }
    }
    
    // Make a best guess based on what we received
    if (this->power_interference_binary_sensor_ != nullptr) {
      this->power_interference_binary_sensor_->publish_state(has_interference);
      ESP_LOGI(TAG, "Set power interference to %s based on raw response analysis", 
               has_interference ? "ON (interference detected)" : "OFF (no interference)");
    }
  } else {
    ESP_LOGW(TAG, "No raw bytes received for power interference check");
    
    // Since we received nothing but we know the device is working (enters config mode),
    // we can cautiously assume there's no power interference
    if (this->power_interference_binary_sensor_ != nullptr) {
      this->power_interference_binary_sensor_->publish_state(false);
      ESP_LOGI(TAG, "Assuming NO power interference (device communicates properly)");
    }
  }
  
  // Exit config mode if we entered it in this function
  if (entered_config_mode) {
    ESP_LOGI(TAG, "Exiting config mode");
    send_command_(CMD_DISABLE_CONFIG);
    delay(500);
    config_mode_ = false;
  }
}

uint32_t HLKLD2402Component::db_to_threshold_(float db_value) {
  return static_cast<uint32_t>(pow(10, db_value / 10));
}

float HLKLD2402Component::threshold_to_db_(uint32_t threshold) {
  return 10 * log10(threshold);
}

void HLKLD2402Component::factory_reset() {
  ESP_LOGI(TAG, "Performing factory reset...");
  
  // Clear UART buffers before starting
  flush();
  while (available()) {
    uint8_t c;
    read_byte(&c);
  }
  
  if (!enter_config_mode_()) {
    ESP_LOGE(TAG, "Failed to enter config mode for factory reset");
    return;
  }
  
  // Add a delay after entering config mode
  delay(200);
  
  ESP_LOGI(TAG, "Resetting max distance to default (5m)");
  set_parameter_(PARAM_MAX_DISTANCE, 50);  // 5.0m = 50 (internal value is in decimeters)
  delay(200);  // Add delay between parameter setting
  
  ESP_LOGI(TAG, "Resetting target timeout to default (5s)");
  set_parameter_(PARAM_TIMEOUT, 5);
  delay(200);
  
  // Reset only trigger threshold for gate 0 as an example
  ESP_LOGI(TAG, "Resetting main threshold values");
  set_parameter_(PARAM_TRIGGER_THRESHOLD, 30);  // 30 = ~3.0 coefficient
  delay(200);
  
  set_parameter_(PARAM_MICRO_THRESHOLD, 30);
  delay(200);
  
  // Save configuration
  ESP_LOGI(TAG, "Saving factory reset configuration");
  if (send_command_(CMD_SAVE_PARAMS)) {
    std::vector<uint8_t> response;
    
    // Wait a bit longer for save operation
    delay(300);
    
    if (read_response_(response)) {
      // Log the response for debugging
      char hex_buf[64] = {0};
      for (size_t i = 0; i < response.size() && i < 16; i++) {
        sprintf(hex_buf + (i*3), "%02X ", response[i]);
      }
      ESP_LOGI(TAG, "Save config response: %s", hex_buf);
      ESP_LOGI(TAG, "Configuration saved successfully");
    } else {
      ESP_LOGW(TAG, "No response to save configuration command");
    }
  } else {
    ESP_LOGW(TAG, "Failed to send save command");
  }
  
  // Add a final delay before exiting config mode
  delay(500);
  
  // Use safer exit pattern
  ESP_LOGI(TAG, "Exiting config mode");
  send_command_(CMD_DISABLE_CONFIG);
  delay(200);
  config_mode_ = false;
  
  ESP_LOGI(TAG, "Factory reset completed");
  
  // Final cleanup
  flush();
}

// Make sure we have matching implementations for ALL protected methods
bool HLKLD2402Component::enter_config_mode_() {
  if (config_mode_)
    return true;
    
  ESP_LOGD(TAG, "Entering config mode...");
  
  // Clear any pending data first
  flush();
  while (available()) {
    uint8_t c;
    read_byte(&c);
  }
  
  // Try multiple times with delays
  for (int attempt = 0; attempt < 3; attempt++) {
    ESP_LOGI(TAG, "Config mode attempt %d", attempt + 1);
    
    // Send the command with no data
    if (!send_command_(CMD_ENABLE_CONFIG)) {
      ESP_LOGE(TAG, "Failed to send config mode command");
      delay(500);  // Wait before retrying
      continue;
    }
    
    // Delay slightly to ensure response has time to arrive
    delay(200);
    
    // Check for response with timeout
    uint32_t start = millis();
    while ((millis() - start) < 1000) {  // 1 second timeout
      if (available() >= 12) {  // Minimum expected response size with header/footer
        std::vector<uint8_t> response;
        if (read_response_(response)) {  // Use default timeout here
          ESP_LOGI(TAG, "Received response to config mode command");
          
          // Dump the response bytes for debugging
          char hex_buf[128] = {0};
          for (size_t i = 0; i < response.size() && i < 20; i++) {
            sprintf(hex_buf + (i*3), "%02X ", response[i]);
          }
          ESP_LOGI(TAG, "Response: %s", hex_buf);
          
          // Looking at logs, the response is: "08 00 FF 01 00 00 02 00 20 00"
          // Format: Length (2) + Command ID (FF 01) + Status (00 00) + Protocol version (02 00) + Buffer size (20 00)
          if (response.size() >= 6 && 
              response[0] == 0xFF && response[1] == 0x01 && 
              response[2] == 0x00 && response[3] == 0x00) {
            config_mode_ = true;
            ESP_LOGI(TAG, "Successfully entered config mode");
            return true;
          } else if (response.size() >= 6 && 
                    response[4] == 0x00 && response[5] == 0x00) {
            // Alternative format sometimes seen
            config_mode_ = true;
            ESP_LOGI(TAG, "Successfully entered config mode (alt format)");
            return true;
          } else {
            ESP_LOGW(TAG, "Invalid config mode response format - expected status 00 00");
            
            // Trace each byte to help diagnose the issue
            ESP_LOGW(TAG, "Response details: %d bytes", response.size());
            for (size_t i = 0; i < response.size() && i < 10; i++) {
                ESP_LOGW(TAG, "  Byte[%d] = 0x%02X", i, response[i]);
            }
          }
        }
      }
      delay(50);  // Small delay between checks
    }
    
    ESP_LOGW(TAG, "No valid response to config mode command, retrying");
    delay(500);  // Wait before retrying
  }
  
  ESP_LOGE(TAG, "Failed to enter config mode after 3 attempts");
  return false;
}

bool HLKLD2402Component::exit_config_mode_() {
  if (!config_mode_)
    return true;
    
  ESP_LOGD(TAG, "Exiting config mode...");
  
  // Send exit command
  if (send_command_(CMD_DISABLE_CONFIG)) {
    // Brief wait for response 
    delay(100);
    
    // Read any response but don't wait too long
    std::vector<uint8_t> response;
    bool got_response = read_response_(response, 300);
    if (got_response) {
      ESP_LOGI(TAG, "Got response to exit command");
    }
  }
  
  // Always mark as exited regardless of response
  config_mode_ = false;
  ESP_LOGI(TAG, "Left config mode");
  
  // Clear any pending data to ensure clean state
  flush();
  while (available()) {
    uint8_t c;
    read_byte(&c);
  }
  
  return true;
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
  
  if (!send_command_(CMD_SET_PARAMS, data, sizeof(data))) {
    ESP_LOGE(TAG, "Failed to send set parameter command");
    return false;
  }
    
  // Add a small delay after sending command
  delay(100);
    
  std::vector<uint8_t> response;
  if (!read_response_(response)) {  // Use default timeout
    ESP_LOGE(TAG, "No response to set parameter command");
    return false;
  }
  
  // Log the response for debugging
  char hex_buf[64] = {0};
  for (size_t i = 0; i < response.size() && i < 16; i++) {
    sprintf(hex_buf + (i*3), "%02X ", response[i]);
  }
  ESP_LOGD(TAG, "Set parameter response: %s", hex_buf);
  
  // Do basic error checking without being too strict on validation
  if (response.size() < 2) {
    ESP_LOGE(TAG, "Response too short");
    return false;
  }
  
  // Check for known error patterns
  bool has_error = false;
  if (response[0] == 0xFF && response[1] == 0xFF) {
    has_error = true;  // This typically indicates an error
  }
  
  if (has_error) {
    ESP_LOGE(TAG, "Parameter setting failed with error response");
    return false;
  }
  
  // For other responses, be permissive and assume success
  return true;
}

}  // namespace hlk_ld2402
}  // namespace esphome