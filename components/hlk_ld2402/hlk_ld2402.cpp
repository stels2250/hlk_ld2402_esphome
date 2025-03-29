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
  
  // Set initial operating mode text
  operating_mode_ = "Normal";
  publish_operating_mode_();
  
  // Initialize the throttle timestamp to avoid updates right after boot
  last_distance_update_ = millis();
}

// New function to passively monitor output for version info
void HLKLD2402Component::begin_passive_version_detection_() {
  ESP_LOGI(TAG, "Starting passive version detection");
  firmware_version_ = "HLK-LD2402"; // Default fallback version
}

// Add method to publish operating mode
void HLKLD2402Component::publish_operating_mode_() {
  if (operating_mode_text_sensor_ != nullptr) {
    operating_mode_text_sensor_->publish_state(operating_mode_);
    ESP_LOGI(TAG, "Published operating mode: %s", operating_mode_.c_str());
  }
}

// Update get_firmware_version_ method to use correct command and parsing
void HLKLD2402Component::get_firmware_version_() {
  ESP_LOGI(TAG, "Retrieving firmware version...");
  
  // Clear any pending data
  flush();
  while (available()) {
    uint8_t c;
    read_byte(&c);
  }
  
  bool entered_config_mode = false;
  
  if (!config_mode_) {
    if (!enter_config_mode_()) {
      ESP_LOGW(TAG, "Failed to enter config mode for firmware version check");
      if (firmware_version_text_sensor_ != nullptr) {
        firmware_version_text_sensor_->publish_state("Unknown - Config Failed");
      }
      return;
    }
    entered_config_mode = true;
  }
  
  // Per protocol spec 5.2.1 - use command 0x0000 for firmware version
  if (send_command_(CMD_GET_VERSION)) {
    delay(300);
    
    std::vector<uint8_t> response;
    if (read_response_(response, 1000)) {
      // According to the protocol, response format: 
      // version_length (2 bytes) + version_string (N bytes)
      if (response.size() >= 2) {
        uint16_t version_length = response[0] | (response[1] << 8);
        
        if (response.size() >= 2 + version_length && version_length > 0) {
          std::string version;
          // Extract version string
          for (size_t i = 2; i < 2 + version_length; i++) {
            version += (char)response[i];
          }
          
          firmware_version_ = version;
          ESP_LOGI(TAG, "Got firmware version: %s", version.c_str());
          
          if (firmware_version_text_sensor_ != nullptr) {
            firmware_version_text_sensor_->publish_state(version);
            ESP_LOGI(TAG, "Published firmware version: %s", version.c_str());
          }
        } else {
          ESP_LOGW(TAG, "Invalid version string length in response");
          if (firmware_version_text_sensor_ != nullptr) {
            firmware_version_text_sensor_->publish_state("Invalid Response");
          }
        }
      } else {
        ESP_LOGW(TAG, "Response too short for version data");
        if (firmware_version_text_sensor_ != nullptr) {
          firmware_version_text_sensor_->publish_state("Invalid Response Format");
        }
      }
    } else {
      ESP_LOGW(TAG, "No response to version command");
      if (firmware_version_text_sensor_ != nullptr) {
        firmware_version_text_sensor_->publish_state("No Response");
      }
    }
  } else {
    ESP_LOGW(TAG, "Failed to send version command");
    if (firmware_version_text_sensor_ != nullptr) {
      firmware_version_text_sensor_->publish_state("Command Failed");
    }
  }
  
  // Exit config mode if we entered it
  if (entered_config_mode) {
    exit_config_mode_();
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
    
    // FIRST CHECK: Check for data frame header (F4 F3 F2 F1)
    if (c == DATA_FRAME_HEADER[0]) {
      // Need to check if this is actually a data frame
      if (available() >= 4) { // Need at least 4 more bytes to verify the header and frame type
        // Peek at the next 4 bytes to check for frame header and type
        bool is_data_frame = true;
        uint8_t peek_bytes[4]; // Header + frame type
        
        // Read the next 4 bytes without removing them from buffer
        for (int i = 0; i < 4; i++) {
          if (!available()) {
            is_data_frame = false;
            break;
          }
          
          read_byte(&peek_bytes[i]);
          
          // First 3 bytes should match header, 4th byte is frame type
          if (i < 3 && peek_bytes[i] != DATA_FRAME_HEADER[i+1]) {
            is_data_frame = false;
            break;
          }
        }
        
        if (is_data_frame) {
          // We have a proper data frame header! Collect the whole frame
          // The 4th byte is the frame type (0x83 for distance data, 0x84 for engineering data)
          uint8_t frame_type = peek_bytes[3];
          
          // Add more verbose logging for engineering mode
          if (operating_mode_ == "Engineering") {
            ESP_LOGI(TAG, "In engineering mode, received frame type: 0x%02X", frame_type);
          }
          
          std::vector<uint8_t> frame_data;
          
          // Add first 5 bytes (4 header + frame type)
          frame_data.push_back(c);  // First byte (already read)
          for (int i = 0; i < 4; i++) {
            frame_data.push_back(peek_bytes[i]);
          }
          
          // Read up to 100 bytes to capture the entire frame
          // This limit is just a safety mechanism - we'll read until we have a complete frame
          size_t max_frame_size = 100;
          size_t bytes_read = 0;
          
          while (available() && bytes_read < max_frame_size) {
            uint8_t data_byte;
            read_byte(&data_byte);
            frame_data.push_back(data_byte);
            bytes_read++;
          }
          
          // Process the data frame based on frame_type
          if (frame_type == DATA_FRAME_TYPE_DISTANCE) {
            if (process_distance_frame_(frame_data)) {
              ESP_LOGD(TAG, "Successfully processed distance data frame");
            } else {
              ESP_LOGW(TAG, "Failed to process distance data frame");
            }
          } else if (frame_type == DATA_FRAME_TYPE_ENGINEERING) {
            if (process_engineering_data_(frame_data)) {
              ESP_LOGD(TAG, "Successfully processed engineering data frame");
            } else {
              ESP_LOGV(TAG, "Failed to process engineering data frame");
            }
          } else {
            ESP_LOGD(TAG, "Unknown frame type: 0x%02X", frame_type);
          }
          
          continue; // Skip further processing for this byte
        } else {
          // Not a valid data frame, add all read bytes to line buffer
          line_buffer_ += (char)c;
          for (int i = 0; i < 4 && !is_data_frame; i++) {
            line_buffer_ += (char)peek_bytes[i];
          }
        }
      }
    }
    
    // SECOND CHECK: Check for command/response frame header (FD FC FB FA)
    else if (c == FRAME_HEADER[0]) {
      // Only consider it a frame header if we have enough bytes available
      // ...existing code for checking command frame...
    }
    
    // Check for text data - add to line buffer
    if (c == '\n') {
      // Process complete line
      if (!line_buffer_.empty()) {
        bool should_process = millis() - last_process_time >= PROCESS_INTERVAL;
        
        if (should_process) {
          last_process_time = millis();
          
          // Less restrictive binary check - only look for obviously non-text chars
          bool is_binary = false;
          for (char ch : line_buffer_) {
            // Only consider control chars below space as binary (except tab and CR)
            if (ch < 32 && ch != '\t' && ch != '\r' && ch != '\n') {
              // Count actual binary characters
              int binary_count = 0;
              for (char c2 : line_buffer_) {
                if (c2 < 32 && c2 != '\t' && c2 != '\r' && c2 != '\n') {
                  binary_count++;
                }
              }
              
              // Only mark as binary if we have several binary chars (>25%)
              if (binary_count > line_buffer_.length() / 4) {
                is_binary = true;
                break;
              }
            }
          }
          
          // Debug - show the line data regardless
          ESP_LOGI(TAG, "Received line [%d bytes]: '%s'", line_buffer_.length(), line_buffer_.c_str());
          if (!is_binary) {
            process_line_(line_buffer_);
          } else {
            ESP_LOGD(TAG, "Skipped binary data that looks like a protocol frame");
            
            // Debug: Show hex representation of binary data
            char hex_buf[128] = {0};
            for (size_t i = 0; i < std::min(line_buffer_.length(), size_t(32)); i++) {
              sprintf(hex_buf + (i*3), "%02X ", (uint8_t)line_buffer_[i]);
            }
            ESP_LOGD(TAG, "Binary data hex: %s", hex_buf);
          }
        }
        line_buffer_.clear();
      }
    } else if (c != '\r') {  // Skip \r
      if (line_buffer_.length() < 1024) {
        line_buffer_ += (char)c;
        
        // Added: Check for direct "distance:" line without proper termination
        if (line_buffer_.length() >= 12 && 
            line_buffer_.compare(line_buffer_.length() - 12, 9, "distance:") == 0) {
          // We found a distance prefix - process the previous data if any
          std::string prev_data = line_buffer_.substr(0, line_buffer_.length() - 12);
          if (!prev_data.empty()) {
            ESP_LOGI(TAG, "Found distance prefix, processing previous data: '%s'", prev_data.c_str());
            process_line_(prev_data);
          }
          // Keep only the distance part
          line_buffer_ = line_buffer_.substr(line_buffer_.length() - 12);
        }
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

  // Check calibration progress if needed
  if (calibration_in_progress_ && calibration_progress_sensor_ != nullptr) {
    uint32_t now = millis();
    if (now - last_calibration_check_ >= 5000) { // Check every 5 seconds
      last_calibration_check_ = now;
      
      if (send_command_(CMD_GET_CALIBRATION_STATUS)) {
        std::vector<uint8_t> response;
        if (read_response_(response)) {
          // Log the complete response for debugging
          char hex_buf[64] = {0};
          for (size_t i = 0; i < response.size() && i < 16; i++) {
            sprintf(hex_buf + (i*3), "%02X ", response[i]);
          }
          ESP_LOGD(TAG, "Calibration status response: %s", hex_buf);
          
          bool handled = false; // Track if we've handled the response
          
          // Handle the actual device response format which differs from the documentation
          // Expected format: 06 00 0A 01 00 00 XX 00 - where XX is the progress value
          if (!handled && response.size() >= 8) {
            // Check for a response that matches the observed pattern
            if (response[0] == 0x06 && response[1] == 0x00 &&
                response[2] == 0x0A && response[3] == 0x01) {
              
              // Extract progress from position 6
              uint16_t progress = response[6]; // Use only the progress byte
              
              ESP_LOGD(TAG, "Raw progress value: 0x%02X (%u)", progress, progress);
              
              // Convert to percentage - appears to be counting up to 100 (0x64)
              uint16_t percentage = (progress * 100) / 0x64;
              
              // Cap to 100% 
              if (percentage > 100) {
                percentage = 100;
              }
              
              ESP_LOGI(TAG, "Calibration progress: %u%% (raw value: %u)", 
                      percentage, progress);
              calibration_progress_ = percentage;
              
              if (this->calibration_progress_sensor_ != nullptr) {
                this->calibration_progress_sensor_->publish_state(percentage);
              }
              
              // Check if calibration is complete (progress value reaches 0x64)
              if (progress >= 0x64) {
                ESP_LOGI(TAG, "Calibration complete");
                calibration_in_progress_ = false;
                exit_config_mode_();
              }
              
              // We successfully processed the response
              handled = true;  // Mark as handled instead of using continue
            }
          }
          
          // Handle the documented response format just in case
          if (!handled && response.size() >= 2 && response[0] == 0x00 && response[1] == 0x00) {
            // According to protocol section 5.2.10:
            // Response format includes 2 bytes ACK status (00 00) followed by 2 bytes percentage
            if (response.size() >= 4) {
              // Read percentage value - little endian (LSB first)
              uint16_t progress = response[2] | (response[3] << 8);
              
              ESP_LOGD(TAG, "Raw progress value (standard format): 0x%04X (%u)", progress, progress);
              
              // Sanity check: ensure progress is 0-100
              if (progress > 100) {
                ESP_LOGW(TAG, "Invalid calibration progress value: %u, capping to 100", progress);
                progress = 100;
              }
              
              calibration_progress_ = progress;
              ESP_LOGI(TAG, "Calibration progress: %u%% (standard format)", progress);
              this->calibration_progress_sensor_->publish_state(progress);
              
              // Check if calibration is complete
              if (progress >= 100) {
                ESP_LOGI(TAG, "Calibration complete");
                calibration_in_progress_ = false;
                exit_config_mode_();
              }
              
              handled = true;  // Mark as handled instead of using continue
            }
          }
          
          // If we reach here and haven't handled the response, show a warning
          if (!handled) {
            ESP_LOGW(TAG, "Unrecognized calibration status response format. Raw bytes:");
            for (size_t i = 0; i < response.size() && i < 16; i++) {
              ESP_LOGW(TAG, "  Byte[%d] = 0x%02X", i, response[i]);
            }
          }
        } else {
          ESP_LOGW(TAG, "No response to calibration status query");
        }
      } else {
        ESP_LOGW(TAG, "Failed to send calibration status query");
      }
    }
  }
}

// Add new method to parse distance data framesith this improved version
bool HLKLD2402Component::process_distance_frame_(const std::vector<uint8_t> &frame_data) {
  // Early throttle check - don't even bother processing if we're throttled
  uint32_t now = millis(); + Length (2) + minimum data
  bool throttled = (this->distance_sensor_ != nullptr && 
                   now - last_distance_update_ < distance_throttle_ms_);));
                   
  if (throttled) {
    ESP_LOGV(TAG, "Skipping binary frame processing due to throttling");
    return false; // Skip processing completely if throttled
  }/ - First 5 bytes are header + type (F4 F3 F2 F1 83)
  // - Next 2 bytes are data length
  // Ensure the frame is at least the minimum expected length;
  // Header (4) + Type (1) + Length (2) + minimum data
  if (frame_data.size() < 10) {one distance value 
    ESP_LOGW(TAG, "Distance frame too short: %d bytes", frame_data.size());
    return false; "Frame too short to contain distance data");
  } return false;
  }
  // Distance data typically follows this format:
  // We'll use the first non-zero value as our distance
  float min_distance_cm = 0;
  
  // Start at byte 13 (index 12) and look for the first valid distance
  for (size_t i = 12; i + 3 < frame_data.size(); i += 4) {
    uint32_t value = frame_data[i] | 
                    (frame_data[i+1] << 8) | 
                    (frame_data[i+2] << 16) | 
                    (frame_data[i+3] << 24);
                    
    // If the value is non-zero, convert to distance
    if (value > 0) {
      float distance = value * 0.1f; // Convert to cm (adjust this factor as needed)
      
      // If this is the first valid value, or it's closer than our current minimum
      if (min_distance_cm == 0 || distance < min_distance_cm) {
        min_distance_cm = distance;
      }
      
      ESP_LOGV(TAG, "Distance value at pos %d: %.1f cm", i, distance);
      
      // Find just one valid value, then break - don't process all values
      if (min_distance_cm > 0) {
        break;
      }
    }
  }
  
  // If we found a valid distance, update our binary sensors (always)
  // and distance sensor (if not throttled)
  if (min_distance_cm > 0) {
    // Extract the detection status from the frame data (typically at position 8)
    uint8_t detection_status = 0;
    if (frame_data.size() >= 9) {
      detection_status = frame_data[8];
    }
    
    // Log with more detailed status information
    const char* status_text = "unknown";
    switch(detection_status) {
      case 0: status_text = "no person"; break;
      case 1: status_text = "person"; break;
      case 2: status_text = "stationary person"; break;
    }

    // Always update binary sensors (no throttling needed)
    update_binary_sensors_(min_distance_cm);
    
    // For distance sensor, apply throttling
    uint32_t now = millis();
    bool throttled = (this->distance_sensor_ != nullptr && 
                     now - last_distance_update_ < distance_throttle_ms_);
    
    if (!throttled && this->distance_sensor_ != nullptr) {
      // Use verbose level for regular updates, INFO only for significant changes
      static float last_reported_distance = 0;
      bool significant_change = fabsf(min_distance_cm - last_reported_distance) > 10.0f;
      
      if (significant_change) {
        ESP_LOGI(TAG, "Detected %s at distance (binary): %.1f cm", status_text, min_distance_cm);
        last_reported_distance = min_distance_cm;
      } else {
        ESP_LOGV(TAG, "Detected %s at distance (binary): %.1f cm", status_text, min_distance_cm);
      }
      
      this->distance_sensor_->publish_state(min_distance_cm);
      last_distance_update_ = now;
      ESP_LOGD(TAG, "Updated distance sensor");
    } else if (throttled) {
      ESP_LOGV(TAG, "Skipping distance sensor update due to throttling (%.1f cm)", min_distance_cm);
    }
    
    return true;
  }
  
  return false;  // No valid distance found
}

// Add new method to process engineering data
bool HLKLD2402Component::process_engineering_data_(const std::vector<uint8_t> &frame_data) {
  // Early exit if engineering data processing is not enabled
  if (!engineering_data_enabled_ || energy_gate_sensors_.empty()) {
    ESP_LOGD(TAG, "Engineering data processing disabled or no sensors configured");
    return false;
  }
  
  // Make sure we're in engineering mode
  if (operating_mode_ != "Engineering") {
    ESP_LOGW(TAG, "Received engineering data frame but not in engineering mode! Current mode: %s", 
            operating_mode_.c_str());
    return false;
  }

  // Ensure the frame is at least the minimum expected length
  // Header (5) + Length (2) + Some data
  if (frame_data.size() < 10) {
    ESP_LOGW(TAG, "Engineering frame too short: %d bytes", frame_data.size());
    return false;
  }
  
  // Always log engineering frames at INFO level for debugging
  char hex_buf[128] = {0};
  for (size_t i = 0; i < std::min(frame_data.size(), size_t(40)); i++) {
    sprintf(hex_buf + (i*3), "%02X ", frame_data[i]);
  }
  ESP_LOGI(TAG, "Engineering frame received: %s", hex_buf);
  
  // Parse the engineering data format
  // As per manual section 5.6.2:
  // Format: F4 F3 F2 F1 84 [Length 2B] [DetectionStatus 1B] [Distance 2B] [GateData 128B] F8 F7 F6 F5
  
  // Verify frame type is engineering data (0x84)
  if (frame_data.size() >= 5 && frame_data[4] != DATA_FRAME_TYPE_ENGINEERING) {
    ESP_LOGW(TAG, "Not an engineering data frame: 0x%02X", frame_data[4]);
    return false;
  }

  // Extract detection status if available (byte 7 after header)
  uint8_t detection_status = 0;
  if (frame_data.size() >= 8) {
    detection_status = frame_data[7];
    const char* status_text = "unknown";
    switch(detection_status) {
      case 0: status_text = "no person"; break;
      case 1: status_text = "person"; break;
      case 2: status_text = "stationary person"; break;
    }
    ESP_LOGD(TAG, "Engineering frame detection status: %s (%d)", status_text, detection_status);
  }

  // Engineering data includes motion energy and micromotion energy for each gate
  // Motion energy starts at byte 10 (each value is 4 bytes)
  // Micromotion energy starts at byte 10 + (32*4) = byte 138

  // Process each gate's energy value
  // We're focusing only on motion energy for now, could add micromotion later
  const size_t motion_energy_start = 10;
  const size_t motion_gate_count = DEFAULT_GATES; // Using 14 gates as determined earlier
  
  for (uint8_t i = 0; i < motion_gate_count; i++) {
    size_t offset = motion_energy_start + (i * 4);
    
    // Make sure we have enough data for this gate
    if (offset + 3 >= frame_data.size()) {
      ESP_LOGW(TAG, "Engineering frame truncated at gate %d", i);
      break;
    }
    
    // Extract 32-bit energy value (little-endian)
    uint32_t raw_energy = frame_data[offset] | 
                        (frame_data[offset+1] << 8) | 
                        (frame_data[offset+2] << 16) | 
                        (frame_data[offset+3] << 24);
    
    // Convert raw energy to dB as per manual: dB = 10 * log10(raw_value)
    float db_energy = 0;
    if (raw_energy > 0) { // Avoid log10(0)
      db_energy = 10.0f * log10f(raw_energy);
    }
    
    // Calculate approximate distance for this gate
    float gate_start_distance = i * DISTANCE_GATE_SIZE;
    float gate_end_distance = gate_start_distance + DISTANCE_GATE_SIZE;
    
    // Update sensor if configured for this gate
    if (i < energy_gate_sensors_.size() && energy_gate_sensors_[i] != nullptr) {
      // Publish either raw value or dB value based on preference
      // For now using dB value which is more intuitive
      energy_gate_sensors_[i]->publish_state(db_energy);
      
      // Log with more context at DEBUG level
      ESP_LOGD(TAG, "Gate %d (%.1f-%.1f m) energy: %.1f dB (raw: %u)", 
              i, gate_start_distance, gate_end_distance, db_energy, raw_energy);
    }
  }
  
  return true;
}

// Create a separate method for updating binary sensors to avoid code duplication
void HLKLD2402Component::update_binary_sensors_(float distance_cm) {
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

void HLKLD2402Component::process_line_(const std::string &line) {
  ESP_LOGD(TAG, "Processing line: '%s'", line.c_str());
  
  // Early throttle check - don't even bother processing if we're throttled
  uint32_t now = millis();
  bool throttled = (this->distance_sensor_ != nullptr && 
                   now - last_distance_update_ < distance_throttle_ms_);
                   
  if (throttled) {
    ESP_LOGV(TAG, "Skipping text processing due to throttling");
    return; // Skip processing completely if throttled
  }
  
  // Handle OFF status
  if (line == "OFF") {
    ESP_LOGI(TAG, "No target detected");e_binary_sensor_->publish_state(false);
    if (this->presence_binary_sensor_ != nullptr) {
      this->presence_binary_sensor_->publish_state(false);
    }e(false);
    if (this->micromovement_binary_sensor_ != nullptr) { }
      this->micromovement_binary_sensor_->publish_state(false);  
    } distance sensor if not throttled
    if (this->distance_sensor_ != nullptr) {lis();
      this->distance_sensor_->publish_state(0);sor_ != nullptr && 
      last_distance_update_ = now; // Update timestamp for throttlingdistance_throttle_ms_);
    }
    return;f (!throttled && this->distance_sensor_ != nullptr) {
  }
ttling
  // Handle different formats of distance data more robustly
  float distance_cm = 0;
  bool valid_distance = false;
  
  // Check if line contains "distance:" anywhere in the stringHandle different formats of distance data more robustly
  size_t dist_pos = line.find("distance:");tance_cm = 0;
  if (dist_pos != std::string::npos) {ool valid_distance = false;
    // Extract everything after "distance:"  
    std::string distance_str = line.substr(dist_pos + 9);his part)
    ESP_LOGV(TAG, "Found distance data: '%s'", distance_str.c_str());
    
    // Remove any trailing non-numeric characters// Check if line contains "distance:" anywhere in the string
    size_t pos = distance_str.find_first_not_of("0123456789.");
  if (dist_pos != std::string::npos) {
    // Extract everything after "distance:"tr(0, pos);
    std::string distance_str = line.substr(dist_pos + 9);
    ESP_LOGV(TAG, "Found distance data: '%s'", distance_str.c_str());
    
    // Remove any trailing non-numeric charactersfloat distance = strtof(distance_str.c_str(), &end);
    size_t pos = distance_str.find_first_not_of("0123456789.");
    if (pos != std::string::npos) {
      distance_str = distance_str.substr(0, pos);e value is already in cm
    }
     valid_distance = true;
    char *end;  
    float distance = strtof(distance_str.c_str(), &end);erbose level for regular updates, INFO only for significant changes
    
    if (end != distance_str.c_str()) {  bool significant_change = fabsf(distance_cm - last_reported_distance) > 10.0f;
      distance_cm = distance;
      valid_distance = true;
    }ed distance (text): %.1f cm", distance_cm);
  } else {ce = distance_cm;
    // Try parsing just a number (some devices output just the number)} else {
    bool is_numeric = true;
    for (char ch : line) {
      if (!isdigit(ch) && ch != '.') {
        is_numeric = false;se {
        break;r (some devices output just the number)
      }
    }
    igit(ch) && ch != '.') {
    if (is_numeric && !line.empty()) {
      char *end; break;
      float distance = strtof(line.c_str(), &end); }
      
      if (end != line.c_str()) {
        distance_cm = distance;.empty()) {
        valid_distance = true;
      }tr(), &end);
    }
  }!= line.c_str()) {
   distance_cm = distance;
  if (valid_distance) {   valid_distance = true;
    // Always update binary sensors     
    update_binary_sensors_(distance_cm);ar updates
    (TAG, "Detected numeric distance: %.1f cm", distance_cm);
    // For distance sensor, apply throttling
    uint32_t now = millis();
    bool throttled = (this->distance_sensor_ != nullptr && 
                     now - last_distance_update_ < distance_throttle_ms_);
    
    if (!throttled && this->distance_sensor_ != nullptr) {pdate distance sensor (already throttled via the early exit above)
      // Use verbose level for regular updates, INFO only for significant changes
      static float last_reported_distance = 0;
      bool significant_change = fabsf(distance_cm - last_reported_distance) > 10.0f;ast_distance_update_ = now;
      
      if (significant_change) { 
        ESP_LOGI(TAG, "Detected distance (text): %.1f cm", distance_cm);  // Always update binary sensors
        last_reported_distance = distance_cm;rs_(distance_cm);
      } else {
        ESP_LOGV(TAG, "Detected distance (text): %.1f cm", distance_cm);
      }
      ig() {
      this->distance_sensor_->publish_state(distance_cm);_LOGCONFIG(TAG, "HLK-LD2402:");
      last_distance_update_ = now;P_LOGCONFIG(TAG, "  Firmware Version: %s", firmware_version_.c_str());
    } else if (throttled) {e: %.1f m", max_distance_);
      ESP_LOGV(TAG, "Skipping distance sensor update due to throttling (%.1f cm)", distance_cm);timeout_);
    }
  }
}bool HLKLD2402Component::write_frame_(const std::vector<uint8_t> &frame) {

void HLKLD2402Component::dump_config() {
  ESP_LOGCONFIG(TAG, "HLK-LD2402:");
  ESP_LOGCONFIG(TAG, "  Firmware Version: %s", firmware_version_.c_str());
  ESP_LOGCONFIG(TAG, "  Max Distance: %.1f m", max_distance_);ite_array returns void
  ESP_LOGCONFIG(TAG, "  Timeout: %u s", timeout_);   written += to_write;
}    if (written < frame.size()) {

bool HLKLD2402Component::write_frame_(const std::vector<uint8_t> &frame) {
  size_t written = 0;
  size_t tries = 0;
  while (written < frame.size() && tries++ < 3) {
    size_t to_write = frame.size() - written;
    write_array(&frame[written], to_write);  // write_array returns void:send_command_(uint16_t command, const uint8_t *data, size_t len) {
    written += to_write;
    if (written < frame.size()) {
      delay(5);Header
    }rame.insert(frame.end(), FRAME_HEADER, FRAME_HEADER + 4);
  }
  return written == frame.size(); // Length (2 bytes) - command (2 bytes) + data length
}  uint16_t total_len = 2 + len;  // 2 for command, plus any additional data

bool HLKLD2402Component::send_command_(uint16_t command, const uint8_t *data, size_t len) {>> 8) & 0xFF);
  std::vector<uint8_t> frame;
  d (2 bytes, little endian)
  // Header
  frame.insert(frame.end(), FRAME_HEADER, FRAME_HEADER + 4);frame.push_back((command >> 8) & 0xFF);
  
  // Length (2 bytes) - command (2 bytes) + data length
  uint16_t total_len = 2 + len;  // 2 for command, plus any additional data
  frame.push_back(total_len & 0xFF);len);
  frame.push_back((total_len >> 8) & 0xFF);}
  
  // Command (2 bytes, little endian)
  frame.push_back(command & 0xFF); FRAME_FOOTER + 4);
  frame.push_back((command >> 8) & 0xFF);
   we're sending for debugging
  // Data (if any)
  if (data != nullptr && len > 0) { i++) {
    frame.insert(frame.end(), data, data + len); sprintf(hex_buf + (i*3), "%02X ", frame[i]);
  }}
  TAG, "Sending command 0x%04X, frame: %s", command, hex_buf);
  // Footer
  frame.insert(frame.end(), FRAME_FOOTER, FRAME_FOOTER + 4);return write_frame_(frame);
  
  // Log the frame we're sending for debugging
  char hex_buf[128] = {0};
  for (size_t i = 0; i < frame.size() && i < 40; i++) {tor<uint8_t> &response, uint32_t timeout_ms) {
    sprintf(hex_buf + (i*3), "%02X ", frame[i]);int32_t start = millis();
  }
  ESP_LOGI(TAG, "Sending command 0x%04X, frame: %s", command, hex_buf);uint8_t header_match = 0;
  
  return write_frame_(frame); 
}  while ((millis() - start) < timeout_ms) {  // Use parameterized timeout

// Modify read_response_ to accept a custom timeout
bool HLKLD2402Component::read_response_(std::vector<uint8_t> &response, uint32_t timeout_ms) {
  uint32_t start = millis();
  std::vector<uint8_t> buffer;
  uint8_t header_match = 0; {
  uint8_t footer_match = 0;      if (c == FRAME_HEADER[header_match]) {
  
  while ((millis() - start) < timeout_ms) {  // Use parameterized timeoutback(c);
    if (available()) {
      uint8_t c;ch = 0;
      read_byte(&c);    buffer.clear();
      
      // Look for header
      if (header_match < 4) {
        if (c == FRAME_HEADER[header_match]) {
          header_match++;
          buffer.push_back(c);
        } else {
          header_match = 0;ER[footer_match]) {
          buffer.clear();ooter_match++;
        }r_match == 4) {
        continue;   // Extract the actual response data (remove header and footer)
      }    response.assign(buffer.begin() + 4, buffer.end() - 4);
      
      buffer.push_back(c);  }
      
      // Look for footer
      if (c == FRAME_FOOTER[footer_match]) {
        footer_match++;
        if (footer_match == 4) {
          // Extract the actual response data (remove header and footer)
          response.assign(buffer.begin() + 4, buffer.end() - 4);
          return true;W(TAG, "Response timeout after %u ms", timeout_ms);
        };
      } else {
        footer_match = 0;
      }dify get_parameter_ to use a longer timeout for power interference parameter
    }02Component::get_parameter_(uint16_t param_id, uint32_t &value) {
    yield();SP_LOGD(TAG, "Getting parameter 0x%04X", param_id);
  }
  
  ESP_LOGW(TAG, "Response timeout after %u ms", timeout_ms);am_id & 0xFF;
  return false; data[1] = (param_id >> 8) & 0xFF;
}  

// Modify get_parameter_ to use a longer timeout for power interference parameter
bool HLKLD2402Component::get_parameter_(uint16_t param_id, uint32_t &value) {
  ESP_LOGD(TAG, "Getting parameter 0x%04X", param_id);}
  
  uint8_t data[2];parameter 0x0005 (power interference)
  data[0] = param_id & 0xFF;FERENCE) {
  data[1] = (param_id >> 8) & 0xFF;  delay(500);  // Wait longer for power interference parameter
  
  if (!send_command_(CMD_GET_PARAMS, data, sizeof(data))) {
    ESP_LOGE(TAG, "Failed to send get parameter command");
    return false;
  }std::vector<uint8_t> response;
  
  // Add a bigger delay for parameter 0x0005 (power interference)_INTERFERENCE) ? 3000 : 1000;
  if (param_id == PARAM_POWER_INTERFERENCE) {
    delay(500);  // Wait longer for power interference parameterd_response_(response, timeout)) {
  } else {");
    delay(100);  // Standard delay for other parameters return false;
  }}
  
  std::vector<uint8_t> response;
  // Use longer timeout for power interference parameter
  uint32_t timeout = (param_id == PARAM_POWER_INTERFERENCE) ? 3000 : 1000;// Log the response for debugging
  
  if (!read_response_(response, timeout)) {
    ESP_LOGE(TAG, "No response to get parameter command");uf + (i*3), "%02X ", response[i]);
    return false;
  }ESP_LOGD(TAG, "Get parameter response: %s", hex_buf);
  
  // ...rest of the function remains the same...// Handle response in a permissive way
  
  // Log the response for debuggingformat
  char hex_buf[64] = {0};] << 16) | (response[5] << 24);
  for (size_t i = 0; i < response.size() && i < 16; i++) {am_id, value);
    sprintf(hex_buf + (i*3), "%02X ", response[i]); return true;
  }
  ESP_LOGD(TAG, "Get parameter response: %s", hex_buf);  // Shorter response, but possibly valid - use first 2 bytes
  < 8);
  // Handle response in a permissive waymeter response, using value: %u", value);
  if (response.size() >= 6) {
    // Standard response format
    value = response[2] | (response[3] << 8) | (response[4] << 16) | (response[5] << 24);
    ESP_LOGD(TAG, "Parameter 0x%04X value: %u", param_id, value);"Invalid parameter response format");
    return true;
  } else if (response.size() >= 2) {
    // Shorter response, but possibly valid - use first 2 bytes
    value = response[0] | (response[1] << 8);
    ESP_LOGW(TAG, "Short parameter response, using value: %u", value);k_mode_with_timeout_(mode, 1000);  // Use default 1000ms timeout
    return true;
  }
  uint32_t mode, uint32_t timeout_ms) {
  ESP_LOGE(TAG, "Invalid parameter response format"); "Setting work mode to %u (0x%X) with %ums timeout", mode, mode, timeout_ms);
  return false; 
}  // Use production mode from manual instead of MODE_NORMAL

bool HLKLD2402Component::set_work_mode_(uint32_t mode) {
  return set_work_mode_with_timeout_(mode, 1000);  // Use default 1000ms timeout   ESP_LOGI(TAG, "Using production mode 0x%X", mode);
}  }

bool HLKLD2402Component::set_work_mode_with_timeout_(uint32_t mode, uint32_t timeout_ms) {
  ESP_LOGI(TAG, "Setting work mode to %u (0x%X) with %ums timeout", mode, mode, timeout_ms);mode_data[0] = 0x00;
  
  // Use production mode from manual instead of MODE_NORMAL;
  if (mode == MODE_NORMAL) {) & 0xFF;
    mode = MODE_PRODUCTION;
    ESP_LOGI(TAG, "Using production mode 0x%X", mode);ode_data[5] = (mode >> 24) & 0xFF;
  }
    ta for debugging
  uint8_t mode_data[6];payload: %02X %02X %02X %02X %02X %02X", 
  mode_data[0] = 0x00;], mode_data[1], mode_data[2], 
  mode_data[1] = 0x00;_data[4], mode_data[5]);
  mode_data[2] = mode & 0xFF;
  mode_data[3] = (mode >> 8) & 0xFF;de_data, sizeof(mode_data))) {
  mode_data[4] = (mode >> 16) & 0xFF;e command");
  mode_data[5] = (mode >> 24) & 0xFF;  return false;
  
  // Log the payload data for debugging
  ESP_LOGD(TAG, "Mode payload: %02X %02X %02X %02X %02X %02X", 
           mode_data[0], mode_data[1], mode_data[2], 
           mode_data[3], mode_data[4], mode_data[5]);if (!read_response_(response, timeout_ms)) {
  meout_ms);
  if (!send_command_(CMD_SET_MODE, mode_data, sizeof(mode_data))) {
    ESP_LOGE(TAG, "Failed to send mode command");
    return false;
  }  // Log the response in detail - improved hex format logging
%02X %02X %02X %02X %02X %02X", 
  // Use the extended timeout for response response[0] : 0, 
  std::vector<uint8_t> response; 0,
  if (!read_response_(response, timeout_ms)) {
    ESP_LOGE(TAG, "No response to mode command (timeout: %ums)", timeout_ms);se.size() > 3 ? response[3] : 0,
    return false;        response.size() > 4 ? response[4] : 0,
  }           response.size() > 5 ? response[5] : 0);

  // Log the response in detail - improved hex format logging
  ESP_LOGI(TAG, "Mode response hex bytes: %02X %02X %02X %02X %02X %02X", 
           response.size() > 0 ? response[0] : 0, 
           response.size() > 1 ? response[1] : 0, && response[1] == 0x00) {
           response.size() > 2 ? response[2] : 0,
           response.size() > 3 ? response[3] : 0,andard ACK)");
           response.size() > 4 ? response[4] : 0,
           response.size() > 5 ? response[5] : 0);// Engineering mode special case - first byte matches requested mode value
  s one format but actual device uses different format
  bool success = false;else if (mode == MODE_ENGINEERING && response.size() >= 3 && 
  ponse[2] == (CMD_SET_MODE & 0xFF)) {
  // Standard success check - ACK is 0x00 0x00
  if (response.size() >= 2 && response[0] == 0x00 && response[1] == 0x00) {[00] [cmd_echo] [01] [00] [00]
    success = true;
    ESP_LOGI(TAG, "Work mode set successfully (standard ACK)"); ESP_LOGI(TAG, "Engineering mode set successfully (device-specific response format)");
  }
  // Engineering mode special case - first byte matches requested mode value
  // Documentation shows one format but actual device uses different format
  else if (mode == MODE_ENGINEERING && response.size() >= 3 && 
           response[0] == (mode & 0xFF) && response[2] == (CMD_SET_MODE & 0xFF)) {
    // The response format for engineering mode appears to be:
    // [mode_byte] [00] [cmd_echo] [01] [00] [00] == MODE_ENGINEERING) {
    success = true;
    ESP_LOGI(TAG, "Engineering mode set successfully (device-specific response format)"); } else {
  }    operating_mode_ = "Unknown";
  
  if (success) {
    // Update the operating mode text
    if (mode == MODE_NORMAL || mode == MODE_PRODUCTION) {
      operating_mode_ = "Normal";
    } else if (mode == MODE_ENGINEERING) {
      operating_mode_ = "Engineering";vailable()) {
    } else {
      operating_mode_ = "Unknown"; read_byte(&c);
    }}
    
    publish_operating_mode_();return true;
    
    // Clear any pending data
    flush();response to set work mode - doesn't match expected patterns");
    while (available()) {
      uint8_t c;
      read_byte(&c);
    } HLKLD2402Component::set_engineering_mode() {
    're already in Engineering mode - if so, switch back to normal
    return true;f (operating_mode_ == "Engineering") {
  }  ESP_LOGI(TAG, "Already in engineering mode, switching back to normal mode");
  
  ESP_LOGE(TAG, "Invalid response to set work mode - doesn't match expected patterns");
  return false; }
}
");
void HLKLD2402Component::set_engineering_mode() {
  // Check if we're already in Engineering mode - if so, switch back to normalnds require this
  if (operating_mode_ == "Engineering") {
    ESP_LOGI(TAG, "Already in engineering mode, switching back to normal mode");led to enter config mode for engineering mode");
    set_normal_mode();
    return;
  }  
 commands:
  ESP_LOGI(TAG, "Switching to engineering mode...");// 1. Clear any pending data
  
  // First enter config mode - most commands require this
  if (!enter_config_mode_()) {
    ESP_LOGE(TAG, "Failed to enter config mode for engineering mode");te(&c);
    return;
  }
  00000004
  // Based on the documentation, make a clean set of commands:eering mode command (0x0012)...");
  // 1. Clear any pending data
  flush();d data: 0x0000 followed by mode 0x00000004
  while (available()) {_data[6];
    uint8_t c;x00;  // First two bytes are 0x0000
    read_byte(&c);ode_data[1] = 0x00;
  }  mode_data[2] = 0x04;  // Engineering mode (0x00000004), little-endian

  // 2. Set engineering mode with command 0x0012, parameter 0x00000004
  ESP_LOGI(TAG, "Sending engineering mode command (0x0012)...");mode_data[5] = 0x00;
  
  // Prepare the command data: 0x0000 followed by mode 0x00000004D_SET_MODE, mode_data, sizeof(mode_data))) {
  uint8_t mode_data[6];mmand");
  mode_data[0] = 0x00;  // First two bytes are 0x0000);
  mode_data[1] = 0x00;
  mode_data[2] = 0x04;  // Engineering mode (0x00000004), little-endian
  mode_data[3] = 0x00;
  mode_data[4] = 0x00;nse with increased timeout
  mode_data[5] = 0x00;std::vector<uint8_t> response;
  
  if (!send_command_(CMD_SET_MODE, mode_data, sizeof(mode_data))) {
    ESP_LOGE(TAG, "Failed to send engineering mode command");
    exit_config_mode_();
    return;
  }
  
  // 3. Wait for response with increased timeout
  std::vector<uint8_t> response; && i < 16; i++) {
  if (!read_response_(response, 2000)) {
    ESP_LOGE(TAG, "No response to engineering mode command");
    exit_config_mode_();TAG, "Engineering mode response: %s", hex_buf);
    return;
  }// Check if the response indicates success
  
  // 4. Validate the response2 && response[0] == 0x00 && response[1] == 0x00) {
  char hex_buf[64] = {0};
  for (size_t i = 0; i < response.size() && i < 16; i++) {y (standard ACK)");
    sprintf(hex_buf + (i*3), "%02X ", response[i]); else if (response.size() >= 3 && response[0] == 0x04 && response[2] == 0x12) {
  }in documentation
  ESP_LOGI(TAG, "Engineering mode response: %s", hex_buf);  success = true;
  essfully (device-specific response format)");
  // Check if the response indicates success
  bool success = false;
  if (response.size() >= 2 && response[0] == 0x00 && response[1] == 0x00) {
    success = true;
    ESP_LOGI(TAG, "Engineering mode set successfully (standard ACK)");
  } else if (response.size() >= 3 && response[0] == 0x04 && response[2] == 0x12) {
    // Special case for engineering mode response as seen in documentation: DO NOT exit config mode - documentation indicates we must
    success = true;
    ESP_LOGI(TAG, "Engineering mode set successfully (device-specific response format)"); ESP_LOGI(TAG, "Entering engineering data monitoring mode. Remaining in config mode.");
  }  config_mode_ = true;
  
  if (success) {vice time to start sending engineering data
    operating_mode_ = "Engineering";
    publish_operating_mode_();
    
    // 5. Important: DO NOT exit config mode - documentation indicates we must
    // remain in config mode for engineering data to be sent
    ESP_LOGI(TAG, "Entering engineering data monitoring mode. Remaining in config mode.");
    config_mode_ = true;  read_byte(&c);
    
    // 6. Add a delay to give the device time to start sending engineering data
    delay(500);ESP_LOGI(TAG, "Engineering mode activated. Waiting for data frames...");
    
    // 7. Clear any pending data again before receiving engineering frames(TAG, "Engineering mode command failed: Unexpected response");
    flush();
    while (available()) {
      uint8_t c;
      read_byte(&c);
    }pdate the normal mode method to handle complete switch
    
    ESP_LOGI(TAG, "Engineering mode activated. Waiting for data frames...");(TAG, "Switching to normal mode...");
  } else {
    ESP_LOGE(TAG, "Engineering mode command failed: Unexpected response");f not already in it
    exit_config_mode_();f (!config_mode_ && !enter_config_mode_()) {
  }   ESP_LOGE(TAG, "Failed to enter config mode for normal mode");
}    return;

// Update the normal mode method to handle complete switch
void HLKLD2402Component::set_normal_mode() {
  ESP_LOGI(TAG, "Switching to normal mode...");if (set_work_mode_(MODE_NORMAL)) {
   normal mode");
  // Enter config mode if not already in it
  if (!config_mode_ && !enter_config_mode_()) {
    ESP_LOGE(TAG, "Failed to enter config mode for normal mode");nfig_mode_();
    return; else {
  }  ESP_LOGE(TAG, "Failed to set normal mode");
  o exit config mode
  // Set work mode
  if (set_work_mode_(MODE_NORMAL)) {
    ESP_LOGI(TAG, "Successfully switched to normal mode");
    
    // Always exit config mode when going to normal mode match new command format and improve progress tracking
    exit_config_mode_();2402Component::calibrate() {
  } else {;
    ESP_LOGE(TAG, "Failed to set normal mode");
    // Still try to exit config mode
    exit_config_mode_();Update calibration to match new command format and improve progress tracking
  }ool HLKLD2402Component::calibrate_with_coefficients(float trigger_coeff, float hold_coeff, float micromotion_coeff) {
}  ESP_LOGI(TAG, "Starting calibration with custom coefficients...");

// Update calibration to match new command format and improve progress tracking
void HLKLD2402Component::calibrate() {;
  calibrate_with_coefficients(3.0f, 3.0f, 3.0f);   return false;
}  }

// Update calibration to match new command format and improve progress tracking
bool HLKLD2402Component::calibrate_with_coefficients(float trigger_coeff, float hold_coeff, float micromotion_coeff) {eff));
  ESP_LOGI(TAG, "Starting calibration with custom coefficients...");hold_coeff = std::max(MIN_COEFF, std::min(MAX_COEFF, hold_coeff));
  (MIN_COEFF, std::min(MAX_COEFF, micromotion_coeff));
  if (!enter_config_mode_()) {
    ESP_LOGE(TAG, "Failed to enter config mode"); section 5.2.9, each coefficient is multiplied by 10
    return false;int16_t trigger_value = static_cast<uint16_t>(trigger_coeff * 10.0f);
  }uint16_t hold_value = static_cast<uint16_t>(hold_coeff * 10.0f);
  omotion_coeff * 10.0f);
  // Clamp coefficients to valid range (1.0 - 20.0)
  trigger_coeff = std::max(MIN_COEFF, std::min(MAX_COEFF, trigger_coeff));bytes
  hold_coeff = std::max(MIN_COEFF, std::min(MAX_COEFF, hold_coeff));
  micromotion_coeff = std::max(MIN_COEFF, std::min(MAX_COEFF, micromotion_coeff));  static_cast<uint8_t>(trigger_value & 0xFF),
  
  // According to section 5.2.9, each coefficient is multiplied by 10
  uint16_t trigger_value = static_cast<uint16_t>(trigger_coeff * 10.0f);
  uint16_t hold_value = static_cast<uint16_t>(hold_coeff * 10.0f);
  uint16_t micro_value = static_cast<uint16_t>(micromotion_coeff * 10.0f);  static_cast<uint8_t>((micro_value >> 8) & 0xFF)
  
  // Prepare data according to the protocol: 3 coefficients, each 2 bytes
  uint8_t data[] = {gger: %.1f, Hold: %.1f, Micro: %.1f", 
    static_cast<uint8_t>(trigger_value & 0xFF),;
    static_cast<uint8_t>((trigger_value >> 8) & 0xFF),
    static_cast<uint8_t>(hold_value & 0xFF),zeof(data))) {
    static_cast<uint8_t>((hold_value >> 8) & 0xFF),ustom coefficients");
    static_cast<uint8_t>(micro_value & 0xFF),
    static_cast<uint8_t>((micro_value >> 8) & 0xFF)// Set calibration flags and initialize progress
  };  calibration_in_progress_ = true;
  
  ESP_LOGI(TAG, "Calibration coefficients - Trigger: %.1f, Hold: %.1f, Micro: %.1f",  status almost immediately
         trigger_coeff, hold_coeff, micromotion_coeff);  
  
  if (send_command_(CMD_START_CALIBRATION, data, sizeof(data))) {
    ESP_LOGI(TAG, "Started calibration with custom coefficients");  this->calibration_progress_sensor_->publish_state(0);
    
    // Set calibration flags and initialize progress
    calibration_in_progress_ = true;
    calibration_progress_ = 0;
    last_calibration_check_ = millis() - 4000; // Check status almost immediatelyESP_LOGE(TAG, "Failed to start calibration");
    
    // Publish initial progress
    if (this->calibration_progress_sensor_ != nullptr) {
      this->calibration_progress_sensor_->publish_state(0);
    }
    mponent::save_config() {
    return true;(TAG, "Saving configuration...");
  } else {
    ESP_LOGE(TAG, "Failed to start calibration");_()) {
    exit_config_mode_(); "Failed to enter config mode");
    return false; return;
  } }
}  

void HLKLD2402Component::save_config() {essfully");
  ESP_LOGI(TAG, "Saving configuration...");} else {
  ave configuration");
  if (!enter_config_mode_()) {
    ESP_LOGE(TAG, "Failed to enter config mode");
    return;xit_config_mode_();
  }
  
  if (save_configuration_()) {
    ESP_LOGI(TAG, "Configuration saved successfully");(TAG, "Sending save configuration command...");
  } else {
    ESP_LOGE(TAG, "Failed to save configuration");f (!send_command_(CMD_SAVE_PARAMS)) {
  }  ESP_LOGE(TAG, "Failed to send save command");
  
  exit_config_mode_(); }
}  

bool HLKLD2402Component::save_configuration_() {
  ESP_LOGI(TAG, "Sending save configuration command...");  ESP_LOGE(TAG, "No response to save command");
  
  if (!send_command_(CMD_SAVE_PARAMS)) {
    ESP_LOGE(TAG, "Failed to send save command");
    return false;/ Per protocol doc 5.3, check ACK status
  }if (response.size() >= 2 && response[0] == 0x00 && response[1] == 0x00) {
  
  std::vector<uint8_t> response;
  if (!read_response_(response)) {
    ESP_LOGE(TAG, "No response to save command");
    return false;
  }
  ommands per documentation section 5.4
  // Per protocol doc 5.3, check ACK status
  if (response.size() >= 2 && response[0] == 0x00 && response[1] == 0x00) {"Enabling auto gain...");
    return true;
  }if (!enter_config_mode_()) {
  G, "Failed to enter config mode");
  return false;   return;
}  }

// Update enable_auto_gain to use correct commands per documentation section 5.4
void HLKLD2402Component::enable_auto_gain() {gain");
  ESP_LOGI(TAG, "Enabling auto gain...");  exit_config_mode_();
  
  if (!enter_config_mode_()) {
    ESP_LOGE(TAG, "Failed to enter config mode");
    return;/ Wait for completion command response (0xF0 command)
  }ESP_LOGI(TAG, "Waiting for auto gain completion...");
  
  if (!enable_auto_gain_()) {
    ESP_LOGE(TAG, "Failed to enable auto gain");
    exit_config_mode_();longer timeout as auto gain may take time to complete
    return;hile ((millis() - start) < 10000) { // 10 second timeout
  }  if (available() >= 12) { // Minimum expected frame size
  
  // Wait for completion command response (0xF0 command)
  ESP_LOGI(TAG, "Waiting for auto gain completion...");>= 2 && response[0] == 0xF0 && response[1] == 0x00) {
  uint32_t start = millis();adjustment completed");
  bool completion_received = false;        completion_received = true;
  
  // Use a longer timeout as auto gain may take time to complete
  while ((millis() - start) < 10000) { // 10 second timeout
    if (available() >= 12) { // Minimum expected frame size
      std::vector<uint8_t> response;
      if (read_response_(response)) {
        if (response.size() >= 2 && response[0] == 0xF0 && response[1] == 0x00) {
          ESP_LOGI(TAG, "Auto gain adjustment completed");
          completion_received = true;, "Auto gain completion notification not received within timeout");
          break;
        }
      }t_config_mode_();
    }
    delay(100);
  }ol HLKLD2402Component::enable_auto_gain_() {
  _GAIN)) {
  if (!completion_received) {
    ESP_LOGW(TAG, "Auto gain completion notification not received within timeout"); return false;
  }}
  
  exit_config_mode_(); std::vector<uint8_t> response;
}  if (!read_response_(response)) {
mmand");
bool HLKLD2402Component::enable_auto_gain_() {
  if (!send_command_(CMD_AUTO_GAIN)) {
    ESP_LOGE(TAG, "Failed to send auto gain command");
    return false;f (response.size() >= 2 && response[0] == 0x00 && response[1] == 0x00) {
  }  ESP_LOGI(TAG, "Auto gain command acknowledged");
  
  std::vector<uint8_t> response;
  if (!read_response_(response)) {
    ESP_LOGE(TAG, "No response to auto gain command");Invalid response to auto gain command");
    return false;eturn false;
  }
  
  if (response.size() >= 2 && response[0] == 0x00 && response[1] == 0x00) {
    ESP_LOGI(TAG, "Auto gain command acknowledged");mponent::get_serial_number() {
    return true;SP_LOGI(TAG, "Getting serial number...");
  }
  
  ESP_LOGE(TAG, "Invalid response to auto gain command");G, "Failed to enter config mode");
  return false;   return;
}  }

// Add serial number retrieval methods
void HLKLD2402Component::get_serial_number() {
  ESP_LOGI(TAG, "Getting serial number...");  // If that fails, try character format
  r_()) {
  if (!enter_config_mode_()) {);
    ESP_LOGE(TAG, "Failed to enter config mode");
    return;
  }
  
  // Try HEX format first (newer firmware)
  if (!get_serial_number_hex_()) {
    // If that fails, try character formatumber_hex_() {
    if (!get_serial_number_char_()) {
      ESP_LOGE(TAG, "Failed to get serial number");SP_LOGE(TAG, "Failed to send hex SN command");
    } return false;
  }}
  
  exit_config_mode_(); std::vector<uint8_t> response;
}  if (!read_response_(response)) {

bool HLKLD2402Component::get_serial_number_hex_() {
  if (!send_command_(CMD_GET_SN_HEX)) {
    ESP_LOGE(TAG, "Failed to send hex SN command");
    return false;/ Per protocol section 5.2.4, response format:
  }// 2 bytes ACK + 2 bytes length + N bytes SN
  sponse[0] == 0x00 && response[1] == 0x00) {
  std::vector<uint8_t> response;2] | (response[3] << 8);
  if (!read_response_(response)) {
    ESP_LOGE(TAG, "No response to hex SN command");size() >= 4 + sn_length) {
    return false;   // Format as hex string
  }    std::string sn;
  
  // Per protocol section 5.2.4, response format:+) {
  // 2 bytes ACK + 2 bytes length + N bytes SN
  if (response.size() >= 4 && response[0] == 0x00 && response[1] == 0x00) {
    uint16_t sn_length = response[2] | (response[3] << 8);  }
    
    if (response.size() >= 4 + sn_length) {
      // Format as hex stringSerial number (hex): %s", sn.c_str());
      std::string sn;
      char temp[8];
      for (size_t i = 4; i < 4 + sn_length; i++) {
        sprintf(temp, "%02X", response[i]);
        sn += temp;n false;
      }
      
      serial_number_ = sn;
      ESP_LOGI(TAG, "Serial number (hex): %s", sn.c_str());d_(CMD_GET_SN_CHAR)) {
      return true;SP_LOGE(TAG, "Failed to send char SN command");
    } return false;
  }}
  
  return false; std::vector<uint8_t> response;
}  if (!read_response_(response)) {

bool HLKLD2402Component::get_serial_number_char_() {
  if (!send_command_(CMD_GET_SN_CHAR)) {
    ESP_LOGE(TAG, "Failed to send char SN command");
    return false;/ Per protocol section 5.2.5, response format:
  }// 2 bytes ACK + 2 bytes length + N bytes SN
  sponse[0] == 0x00 && response[1] == 0x00) {
  std::vector<uint8_t> response;2] | (response[3] << 8);
  if (!read_response_(response)) {
    ESP_LOGE(TAG, "No response to char SN command");size() >= 4 + sn_length) {
    return false;   // Format as character string
  }    std::string sn;
  {
  // Per protocol section 5.2.5, response format:
  // 2 bytes ACK + 2 bytes length + N bytes SN
  if (response.size() >= 4 && response[0] == 0x00 && response[1] == 0x00) {
    uint16_t sn_length = response[2] | (response[3] << 8);  serial_number_ = sn;
    %s", sn.c_str());
    if (response.size() >= 4 + sn_length) {
      // Format as character string
      std::string sn;
      for (size_t i = 4; i < 4 + sn_length; i++) {
        sn += (char)response[i];n false;
      }
      
      serial_number_ = sn;
      ESP_LOGI(TAG, "Serial number (char): %s", sn.c_str());hecking power interference status");
      return true;
    }/ Clear any pending data first
  }flush();
  ble()) {
  return false;   uint8_t c;
}    read_byte(&c);

void HLKLD2402Component::check_power_interference() {
  ESP_LOGI(TAG, "Checking power interference status");// Flag to track if we entered config mode in this function
  e;
  // Clear any pending data first
  flush();
  while (available()) {_config_mode_()) {
    uint8_t c;, "Failed to enter config mode for power interference check");
    read_byte(&c);   
  }    // Show ERROR state if the check fails
   {
  // Flag to track if we entered config mode in this functionnary_sensor_->publish_state(true);  // Show interference/error
  bool entered_config_mode = false;      ESP_LOGE(TAG, "Setting power interference to ON (ERROR) due to config mode failure");
  
  if (!config_mode_) {
    if (!enter_config_mode_()) {
      ESP_LOGE(TAG, "Failed to enter config mode for power interference check");tered_config_mode = true;
      
      // Show ERROR state if the check fails
      if (this->power_interference_binary_sensor_ != nullptr) {
        this->power_interference_binary_sensor_->publish_state(true);  // Show interference/error
        ESP_LOGE(TAG, "Setting power interference to ON (ERROR) due to config mode failure");
      }am_data[2];
      return;am_data[0] = PARAM_POWER_INTERFERENCE & 0xFF;  // 0x05
    }INTERFERENCE >> 8) & 0xFF;  // 0x00
    entered_config_mode = true;
  }if (!send_command_(CMD_GET_PARAMS, param_data, sizeof(param_data))) {
  
  // According to documentation, use GET_PARAMS command (0x0008) with parameter ID 0x0005
  ESP_LOGI(TAG, "Reading power interference parameter...");  if (this->power_interference_binary_sensor_ != nullptr) {
  erence_binary_sensor_->publish_state(true);  // Show interference/error
  uint8_t param_data[2];R) due to command failure");
  param_data[0] = PARAM_POWER_INTERFERENCE & 0xFF;  // 0x05
  param_data[1] = (PARAM_POWER_INTERFERENCE >> 8) & 0xFF;  // 0x00  
  
  if (!send_command_(CMD_GET_PARAMS, param_data, sizeof(param_data))) {
    ESP_LOGE(TAG, "Failed to send power interference parameter query");  exit_config_mode_();
    
    if (this->power_interference_binary_sensor_ != nullptr) {
      this->power_interference_binary_sensor_->publish_state(true);  // Show interference/error
      ESP_LOGE(TAG, "Setting power interference to ON (ERROR) due to command failure");
    } Wait for response
    
    // Exit config mode if we entered it
    if (entered_config_mode) {ponse;
      exit_config_mode_();(!read_response_(response, 2000)) {
    }E(TAG, "No response to power interference parameter query");
    return; 
  }  if (this->power_interference_binary_sensor_ != nullptr) {
  rference_binary_sensor_->publish_state(true);  // Show interference/error
  // Wait for responseE(TAG, "Setting power interference to ON (ERROR) due to timeout");
  delay(500);  }
  
  std::vector<uint8_t> response;
  if (!read_response_(response, 2000)) {
    ESP_LOGE(TAG, "No response to power interference parameter query");  exit_config_mode_();
    
    if (this->power_interference_binary_sensor_ != nullptr) {
      this->power_interference_binary_sensor_->publish_state(true);  // Show interference/error
      ESP_LOGE(TAG, "Setting power interference to ON (ERROR) due to timeout");
    } Log the response for debugging
    
    // Exit config mode if we entered itnse.size() && i < 30; i++) {
    if (entered_config_mode) {), "%02X ", response[i]);
      exit_config_mode_();
    }TAG, "Power interference response: %s", hex_buf);
    return;
  }// According to documentation:
  
  // Log the response for debugging
  char hex_buf[128] = {0};
  for (size_t i = 0; i < response.size() && i < 30; i++) {
    sprintf(hex_buf + (i*3), "%02X ", response[i]);
  }
  ESP_LOGI(TAG, "Power interference response: %s", hex_buf);// The response format is:
  s) + Parameter ID (2 bytes) + Parameter value (4 bytes)
  // According to documentation: >= 10) {
  // 0: Not performeds at offset 6-9, little endian
  // 1: No interference ponse[6] | (response[7] << 8) | (response[8] << 16) | (response[9] << 24);
  // 2: Has interferencerence value: %u", value);
  bool has_interference = false;  
  
  // Based on the protocol documentation and our response:nterference check not performed");
  // The response format is:
  // CMD (2 bytes) + ACK (2 bytes) + Parameter ID (2 bytes) + Parameter value (4 bytes)
  if (response.size() >= 10) {
    // Parameter value is at offset 6-9, little endian
    uint32_t value = response[6] | (response[7] << 8) | (response[8] << 16) | (response[9] << 24);
    ESP_LOGI(TAG, "Power interference value: %u", value);  ESP_LOGI(TAG, "Power interference detected");
    e = true;
    if (value == 0) {
      ESP_LOGI(TAG, "Power interference check not performed");wer interference value: %u", value);
      has_interference = false;lue != 1);  // Consider anything other than 1 as interference
    } else if (value == 1) {
      ESP_LOGI(TAG, "No power interference detected");
      has_interference = false;ower interference parameter response format");
    } else if (value == 2) {e on invalid response
      ESP_LOGI(TAG, "Power interference detected");
      has_interference = true;
    } else {
      ESP_LOGW(TAG, "Unknown power interference value: %u", value);
      has_interference = (value != 1);  // Consider anything other than 1 as interferencehis->power_interference_binary_sensor_->publish_state(has_interference);
    }GI(TAG, "Set power interference to %s based on parameter value", 
  } else {terference)");
    ESP_LOGW(TAG, "Invalid power interference parameter response format");
    has_interference = true;  // Assume interference on invalid response
  }// Exit config mode if we entered it in this function
  e) {
  // Update sensor state
  if (this->power_interference_binary_sensor_ != nullptr) {
    this->power_interference_binary_sensor_->publish_state(has_interference);
    ESP_LOGI(TAG, "Set power interference to %s based on parameter value", 
             has_interference ? "ON (interference detected)" : "OFF (no interference)");t32_t HLKLD2402Component::db_to_threshold_(float db_value) {
  }return static_cast<uint32_t>(pow(10, db_value / 10));
  
  // Exit config mode if we entered it in this function
  if (entered_config_mode) {::threshold_to_db_(uint32_t threshold) {
    exit_config_mode_();eturn 10 * log10(threshold);
  }
}

uint32_t HLKLD2402Component::db_to_threshold_(float db_value) {
  return static_cast<uint32_t>(pow(10, db_value / 10)); 
}  // Clear UART buffers before starting

float HLKLD2402Component::threshold_to_db_(uint32_t threshold) {
  return 10 * log10(threshold);   uint8_t c;
}    read_byte(&c);

void HLKLD2402Component::factory_reset() {
  ESP_LOGI(TAG, "Performing factory reset...");if (!enter_config_mode_()) {
  ig mode for factory reset");
  // Clear UART buffers before starting;
  flush();
  while (available()) {
    uint8_t c;fter entering config mode
    read_byte(&c);elay(200);
  }
   distance to default (5m)");
  if (!enter_config_mode_()) {alue is in decimeters)
    ESP_LOGE(TAG, "Failed to enter config mode for factory reset"););  // Add delay between parameter setting
    return;
  }ESP_LOGI(TAG, "Resetting target timeout to default (5s)");
  
  // Add a delay after entering config mode
  delay(200);
  
  ESP_LOGI(TAG, "Resetting max distance to default (5m)");
  set_parameter_(PARAM_MAX_DISTANCE, 50);  // 5.0m = 50 (internal value is in decimeters) = ~3.0 coefficient
  delay(200);  // Add delay between parameter settingdelay(200);
  
  ESP_LOGI(TAG, "Resetting target timeout to default (5s)");OLD, 30);
  set_parameter_(PARAM_TIMEOUT, 5);
  delay(200);
  
  // Reset only trigger threshold for gate 0 as an example");
  ESP_LOGI(TAG, "Resetting main threshold values");
  set_parameter_(PARAM_TRIGGER_THRESHOLD, 30);  // 30 = ~3.0 coefficientor<uint8_t> response;
  delay(200);  
  
  set_parameter_(PARAM_MICRO_THRESHOLD, 30););
  delay(200);  
  response)) {
  // Save configuration
  ESP_LOGI(TAG, "Saving factory reset configuration");
  if (send_command_(CMD_SAVE_PARAMS)) {nse.size() && i < 16; i++) {
    std::vector<uint8_t> response;    sprintf(hex_buf + (i*3), "%02X ", response[i]);
    
    // Wait a bit longer for save operationTAG, "Save config response: %s", hex_buf);
    delay(300);  ESP_LOGI(TAG, "Configuration saved successfully");
    
    if (read_response_(response)) {e configuration command");
      // Log the response for debugging
      char hex_buf[64] = {0};
      for (size_t i = 0; i < response.size() && i < 16; i++) {
        sprintf(hex_buf + (i*3), "%02X ", response[i]);
      }
      ESP_LOGI(TAG, "Save config response: %s", hex_buf);
      ESP_LOGI(TAG, "Configuration saved successfully");;
    } else {
      ESP_LOGW(TAG, "No response to save configuration command");Use safer exit pattern
    }(TAG, "Exiting config mode");
  } else {
    ESP_LOGW(TAG, "Failed to send save command");elay(200);
  }config_mode_ = false;
  
  // Add a final delay before exiting config modeG, "Factory reset completed");
  delay(500);
  
  // Use safer exit pattern
  ESP_LOGI(TAG, "Exiting config mode");
  send_command_(CMD_DISABLE_CONFIG);
  delay(200);tching implementations for ALL protected methods
  config_mode_ = false;ol HLKLD2402Component::enter_config_mode_() {
  
  ESP_LOGI(TAG, "Factory reset completed");  return true;
  
  // Final cleanup(TAG, "Entering config mode...");
  flush(); 
}  // Clear any pending data first

// Make sure we have matching implementations for ALL protected methods
bool HLKLD2402Component::enter_config_mode_() {
  if (config_mode_));
    return true;
    
  ESP_LOGD(TAG, "Entering config mode...");// Try multiple times with delays
   3; attempt++) {
  // Clear any pending data firstGI(TAG, "Config mode attempt %d", attempt + 1);
  flush();
  while (available()) {e command with no data
    uint8_t c;and_(CMD_ENABLE_CONFIG)) {
    read_byte(&c);   ESP_LOGE(TAG, "Failed to send config mode command");
  }    delay(500);  // Wait before retrying
  
  // Try multiple times with delays
  for (int attempt = 0; attempt < 3; attempt++) {
    ESP_LOGI(TAG, "Config mode attempt %d", attempt + 1);// Delay slightly to ensure response has time to arrive
    
    // Send the command with no data
    if (!send_command_(CMD_ENABLE_CONFIG)) {
      ESP_LOGE(TAG, "Failed to send config mode command");
      delay(500);  // Wait before retryinglis() - start) < 1000) {  // 1 second timeout
      continue; if (available() >= 12) {  // Minimum expected response size with header/footer
    }    std::vector<uint8_t> response;
    eout here
    // Delay slightly to ensure response has time to arriveOGI(TAG, "Received response to config mode command");
    delay(200);      
    or debugging
    // Check for response with timeout{0};
    uint32_t start = millis();i++) {
    while ((millis() - start) < 1000) {  // 1 second timeout
      if (available() >= 12) {  // Minimum expected response size with header/footer
        std::vector<uint8_t> response;
        if (read_response_(response)) {  // Use default timeout here
          ESP_LOGI(TAG, "Received response to config mode command");// Looking at logs, the response is: "08 00 FF 01 00 00 02 00 20 00"
          1) + Status (00 00) + Protocol version (02 00) + Buffer size (20 00)
          // Dump the response bytes for debugging && 
          char hex_buf[128] = {0};
          for (size_t i = 0; i < response.size() && i < 20; i++) {
            sprintf(hex_buf + (i*3), "%02X ", response[i]); config_mode_ = true;
          }onfig mode");
          ESP_LOGI(TAG, "Response: %s", hex_buf);  
          
          // Looking at logs, the response is: "08 00 FF 01 00 00 02 00 20 00"
          // Format: Length (2) + Command ID (FF 01) + Status (00 00) + Protocol version (02 00) + Buffer size (20 00)
          if (response.size() >= 6 && 
              response[0] == 0xFF && response[1] == 0x01 && 
              response[2] == 0x00 && response[3] == 0x00) {ze() >= 6 && 
            config_mode_ = true;) {
            ESP_LOGI(TAG, "Successfully entered config mode");// Alternative format sometimes seen
            
            // Update operating mode entered config mode (alt format)");
            operating_mode_ = "Config";
            publish_operating_mode_();// Update operating mode
            de_ = "Config";
            return true;
          } else if (response.size() >= 6 && 
                    response[4] == 0x00 && response[5] == 0x00) {
            // Alternative format sometimes seen
            config_mode_ = true;tatus 00 00");
            ESP_LOGI(TAG, "Successfully entered config mode (alt format)");
            lp diagnose the issue
            // Update operating modeails: %d bytes", response.size());
            operating_mode_ = "Config";ponse.size() && i < 10; i++) {
            publish_operating_mode_();    ESP_LOGW(TAG, "  Byte[%d] = 0x%02X", i, response[i]);
            
            return true;
          } else {
            ESP_LOGW(TAG, "Invalid config mode response format - expected status 00 00");
            
            // Trace each byte to help diagnose the issue
            ESP_LOGW(TAG, "Response details: %d bytes", response.size());
            for (size_t i = 0; i < response.size() && i < 10; i++) {");
                ESP_LOGW(TAG, "  Byte[%d] = 0x%02X", i, response[i]););  // Wait before retrying
            }
          }
        }OGE(TAG, "Failed to enter config mode after 3 attempts");
      }
      delay(50);  // Small delay between checks
    }
    
    ESP_LOGW(TAG, "No valid response to config mode command, retrying");
    delay(500);  // Wait before retrying return true;
  }  
  
  ESP_LOGE(TAG, "Failed to enter config mode after 3 attempts");
  return false; // Send exit command
}  if (send_command_(CMD_DISABLE_CONFIG)) {

bool HLKLD2402Component::exit_config_mode_() {
  if (!config_mode_)
    return true;// Read any response but don't wait too long
    
  ESP_LOGD(TAG, "Exiting config mode...");  bool got_response = read_response_(response, 300);
  {
  // Send exit commandcommand");
  if (send_command_(CMD_DISABLE_CONFIG)) {
    // Brief wait for response 
    delay(100);
    e
    // Read any response but don't wait too long
    std::vector<uint8_t> response;
    bool got_response = read_response_(response, 300);
    if (got_response) {Engineering
      ESP_LOGI(TAG, "Got response to exit command");(operating_mode_ == "Config") {
    } operating_mode_ = "Normal";  // Default to Normal when exiting config mode
  }  publish_operating_mode_();
  
  // Always mark as exited regardless of response
  config_mode_ = false;e clean state
  ESP_LOGI(TAG, "Left config mode");flush();
  
  // Update operating mode back to either Normal or Engineering
  if (operating_mode_ == "Config") {
    operating_mode_ = "Normal";  // Default to Normal when exiting config mode
    publish_operating_mode_();
  }return true;
  
  // Clear any pending data to ensure clean state
  flush();::set_parameter_(uint16_t param_id, uint32_t value) {
  while (available()) {, "Setting parameter 0x%04X to %u", param_id, value);
    uint8_t c;
    read_byte(&c);int8_t data[6];
  }data[0] = param_id & 0xFF;
  aram_id >> 8) & 0xFF;
  return true; data[2] = value & 0xFF;
}  data[3] = (value >> 8) & 0xFF;

bool HLKLD2402Component::set_parameter_(uint16_t param_id, uint32_t value) {
  ESP_LOGD(TAG, "Setting parameter 0x%04X to %u", param_id, value);
  d_(CMD_SET_PARAMS, data, sizeof(data))) {
  uint8_t data[6]; send set parameter command");
  data[0] = param_id & 0xFF;
  data[1] = (param_id >> 8) & 0xFF;
  data[2] = value & 0xFF;
  data[3] = (value >> 8) & 0xFF;ing command
  data[4] = (value >> 16) & 0xFF;
  data[5] = (value >> 24) & 0xFF;  
  
  if (!send_command_(CMD_SET_PARAMS, data, sizeof(data))) {
    ESP_LOGE(TAG, "Failed to send set parameter command"); "No response to set parameter command");
    return false; return false;
  }
    
  // Add a small delay after sending commandresponse for debugging
  delay(100);ar hex_buf[64] = {0};
    e.size() && i < 16; i++) {
  std::vector<uint8_t> response;
  if (!read_response_(response)) {  // Use default timeout
    ESP_LOGE(TAG, "No response to set parameter command");Set parameter response: %s", hex_buf);
    return false;
  }// Do basic error checking without being too strict on validation
  
  // Log the response for debuggingse too short");
  char hex_buf[64] = {0};
  for (size_t i = 0; i < response.size() && i < 16; i++) {
    sprintf(hex_buf + (i*3), "%02X ", response[i]);
  }
  ESP_LOGD(TAG, "Set parameter response: %s", hex_buf);bool has_error = false;
  
  // Do basic error checking without being too strict on validationis typically indicates an error
  if (response.size() < 2) {
    ESP_LOGE(TAG, "Response too short");
    return false;f (has_error) {
  }  ESP_LOGE(TAG, "Parameter setting failed with error response");
  
  // Check for known error patterns
  bool has_error = false;
  if (response[0] == 0xFF && response[1] == 0xFF) {
    has_error = true;  // This typically indicates an erroreturn true;
  }
  
  if (has_error) {
    ESP_LOGE(TAG, "Parameter setting failed with error response");ponent::set_motion_threshold(uint8_t gate, float db_value) {
    return false;SP_LOGI(TAG, "Setting motion threshold for gate %d to %.1f dB", gate, db_value);
  }
  
  // For other responses, be permissive and assume successAG, "Invalid gate index %d (must be 0-15)", gate);
  return true;   return false;
}  }

// Add these methods to configure thresholds for specific gates
bool HLKLD2402Component::set_motion_threshold(uint8_t gate, float db_value) {
  ESP_LOGI(TAG, "Setting motion threshold for gate %d to %.1f dB", gate, db_value);
  ue to raw threshold
  if (gate >= 16) {
    ESP_LOGE(TAG, "Invalid gate index %d (must be 0-15)", gate);
    return false;/ Gate-specific parameter ID: 0x0010 + gate number
  }uint16_t param_id = PARAM_TRIGGER_THRESHOLD + gate;
  
  // Clamp dB value to valid range (0-95 dB according to documentation)
  db_value = std::max(0.0f, std::min(95.0f, db_value));bool entered_config = false;
  
  // Convert dB value to raw threshold
  uint32_t threshold = db_to_threshold_(db_value);    ESP_LOGE(TAG, "Failed to enter config mode for threshold setting");
  
  // Gate-specific parameter ID: 0x0010 + gate number
  uint16_t param_id = PARAM_TRIGGER_THRESHOLD + gate;  entered_config = true;
  
  // Need to be in config mode to set parameters
  bool entered_config = false;arameter_(param_id, threshold);
  if (!config_mode_) {
    if (!enter_config_mode_()) {
      ESP_LOGE(TAG, "Failed to enter config mode for threshold setting");Successfully set motion threshold for gate %d to %.1f dB (raw: %u)",
      return false;        gate, db_value, threshold);
    }
    entered_config = true; ESP_LOGE(TAG, "Failed to set motion threshold");
  }}
  
  bool success = set_parameter_(param_id, threshold);// Exit config mode if we entered it
  nfig) {
  if (success) {
    ESP_LOGI(TAG, "Successfully set motion threshold for gate %d to %.1f dB (raw: %u)",
             gate, db_value, threshold);
  } else {
    ESP_LOGE(TAG, "Failed to set motion threshold");
  }
  on_threshold(uint8_t gate, float db_value) {
  // Exit config mode if we entered itg micromotion threshold for gate %d to %.1f dB", gate, db_value);
  if (entered_config) {
    exit_config_mode_();f (gate >= 16) {
  }  ESP_LOGE(TAG, "Invalid gate index %d (must be 0-15)", gate);
  
  return success; }
}  

bool HLKLD2402Component::set_micromotion_threshold(uint8_t gate, float db_value) {
  ESP_LOGI(TAG, "Setting micromotion threshold for gate %d to %.1f dB", gate, db_value);
  ue to raw threshold
  if (gate >= 16) {
    ESP_LOGE(TAG, "Invalid gate index %d (must be 0-15)", gate);
    return false;/ Gate-specific parameter ID: 0x0030 + gate number
  }uint16_t param_id = PARAM_MICRO_THRESHOLD + gate;
  
  // Clamp dB value to valid range (0-95 dB according to documentation)
  db_value = std::max(0.0f, std::min(95.0f, db_value));bool entered_config = false;
  
  // Convert dB value to raw threshold
  uint32_t threshold = db_to_threshold_(db_value);    ESP_LOGE(TAG, "Failed to enter config mode for threshold setting");
  
  // Gate-specific parameter ID: 0x0030 + gate number
  uint16_t param_id = PARAM_MICRO_THRESHOLD + gate;  entered_config = true;
  
  // Need to be in config mode to set parameters
  bool entered_config = false;arameter_(param_id, threshold);
  if (!config_mode_) {
    if (!enter_config_mode_()) {
      ESP_LOGE(TAG, "Failed to enter config mode for threshold setting");Successfully set micromotion threshold for gate %d to %.1f dB (raw: %u)",
      return false;        gate, db_value, threshold);
    }
    entered_config = true; ESP_LOGE(TAG, "Failed to set micromotion threshold");
  }}
  
  bool success = set_parameter_(param_id, threshold);// Exit config mode if we entered it
  nfig) {
  if (success) {
    ESP_LOGI(TAG, "Successfully set micromotion threshold for gate %d to %.1f dB (raw: %u)",
             gate, db_value, threshold);
  } else {
    ESP_LOGE(TAG, "Failed to set micromotion threshold");
  }
  
  // Exit config mode if we entered it  if (entered_config) {    exit_config_mode_();  }    return success;}}  // namespace hlk_ld2402
}  // namespace esphome