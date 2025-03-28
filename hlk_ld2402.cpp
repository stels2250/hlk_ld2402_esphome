#include "hlk_ld2402.h"

static const char *const TAG = "hlk_ld2402";

// Protocol constants
static const uint8_t FRAME_HEADER[4] = {0xFD, 0xFC, 0xFB, 0xFA};
static const uint8_t FRAME_FOOTER[4] = {0x04, 0x03, 0x02, 0x01};
static const uint8_t DATA_HEADER[4] = {0xF4, 0xF3, 0xF2, 0xF1};
static const uint8_t DATA_FOOTER[4] = {0xF8, 0xF7, 0xF6, 0xF5};

void HLK_LD2402::setup() {
  ESP_LOGCONFIG(TAG, "Setting up HLK-LD2402...");
  
  // Initialize sensors if they exist
  if (presence_sensor) presence_sensor->publish_state(false);
  if (distance_sensor) distance_sensor->publish_state(NAN);
  if (movement_sensor) movement_sensor->publish_state(false);
  if (micromovement_sensor) micromovement_sensor->publish_state(false);
  
  // Start configuration process
  initialized_ = false;
}

void HLK_LD2402::loop() {
  // Read available data
  while (available()) {
    uint8_t c;
    read_byte(&c);
    buffer_.push_back(c);

    // Check if we have a complete frame
    if (buffer_.size() >= 8) {  // Minimum frame size
      // Check for data frame
      if (buffer_.size() >= 4 && 
          memcmp(&buffer_[buffer_.size() - 4], DATA_FOOTER, 4) == 0) {
        process_data_(buffer_);
        buffer_.clear();
      }
      // Check for command response frame
      else if (buffer_.size() >= 4 && 
               memcmp(&buffer_[buffer_.size() - 4], FRAME_FOOTER, 4) == 0) {
        // Process command response if needed
        buffer_.clear();
      }
    }

    // Prevent buffer from growing too large
    if (buffer_.size() > 128) {
      buffer_.clear();
    }
  }

  // Initialization sequence
  if (!initialized_ && millis() > 5000) {  // Wait 5 seconds before init
    enable_config_mode_();
    read_firmware_version_();
    configure_sensor_();
    disable_config_mode_();
    initialized_ = true;
    ESP_LOGI(TAG, "HLK-LD2402 initialized");
  }
}

void HLK_LD2402::process_data_(const std::vector<uint8_t> &data) {
  // Check frame length
  if (data.size() < 8) return;

  // Check header
  if (memcmp(&data[0], DATA_HEADER, 4) != 0) return;

  // Get frame length (little endian)
  uint16_t frame_len = data[4] | (data[5] << 8);
  if (data.size() < 8 + frame_len) return;

  // Parse detection result (byte 6)
  bool presence = (data[6] == 0x01 || data[6] == 0x02);
  
  // Parse distance (bytes 7-8, little endian, in cm)
  float distance = (data[7] | (data[8] << 8)) / 100.0f;

  // Publish sensor states
  if (presence_sensor) presence_sensor->publish_state(presence);
  if (distance_sensor) distance_sensor->publish_state(distance);
  
  // Movement state (simplified - actual implementation may need more data)
  if (movement_sensor) movement_sensor->publish_state(presence);
  
  // Micro-movement would require parsing additional data fields
}

void HLK_LD2402::send_command_(const std::vector<uint8_t> &command) {
  std::vector<uint8_t> frame;
  
  // Add header
  frame.insert(frame.end(), FRAME_HEADER, FRAME_HEADER + 4);
  
  // Add length (little endian)
  uint16_t len = command.size();
  frame.push_back(len & 0xFF);
  frame.push_back((len >> 8) & 0xFF);
  
  // Add command
  frame.insert(frame.end(), command.begin(), command.end());
  
  // Add footer
  frame.insert(frame.end(), FRAME_FOOTER, FRAME_FOOTER + 4);
  
  // Send frame
  write_array(frame);
}

void HLK_LD2402::enable_config_mode_() {
  ESP_LOGD(TAG, "Enabling config mode");
  std::vector<uint8_t> cmd = {0xFF, 0x00, 0x01, 0x00};  // Command 0x00FF, value 0x0001
  send_command_(cmd);
  config_mode_ = true;
  delay(100);  // Wait for response
}

void HLK_LD2402::disable_config_mode_() {
  ESP_LOGD(TAG, "Disabling config mode");
  std::vector<uint8_t> cmd = {0xFE, 0x00};  // Command 0x00FE
  send_command_(cmd);
  config_mode_ = false;
  delay(100);  // Wait for response
}

void HLK_LD2402::read_firmware_version_() {
  ESP_LOGD(TAG, "Reading firmware version");
  std::vector<uint8_t> cmd = {0x00, 0x00};  // Command 0x0000
  send_command_(cmd);
  delay(100);  // Wait for response
}

void HLK_LD2402::configure_sensor_() {
  if (!config_mode_) return;

  ESP_LOGD(TAG, "Configuring sensor");
  
  // Set max distance (command 0x0007, parameter 0x0001)
  // Value is in 0.1m units (e.g., 8.5m = 85)
  uint16_t max_dist = static_cast<uint16_t>(max_distance_ * 10);
  std::vector<uint8_t> dist_cmd = {
    0x07, 0x00,  // Command 0x0007
    0x01, 0x00,  // Parameter ID 0x0001 (max distance)
    static_cast<uint8_t>(max_dist & 0xFF), 
    static_cast<uint8_t>((max_dist >> 8) & 0xFF),
    0x00, 0x00   // Remainder of 4-byte value
  };
  send_command_(dist_cmd);
  delay(100);
  
  // Set disappear delay (command 0x0007, parameter 0x0004)
  std::vector<uint8_t> delay_cmd = {
    0x07, 0x00,  // Command 0x0007
    0x04, 0x00,  // Parameter ID 0x0004 (disappear delay)
    static_cast<uint8_t>(disappear_delay_ & 0xFF), 
    static_cast<uint8_t>((disappear_delay_ >> 8) & 0xFF),
    0x00, 0x00   // Remainder of 4-byte value
  };
  send_command_(delay_cmd);
  delay(100);
  
  // Save parameters (command 0x00FD)
  std::vector<uint8_t> save_cmd = {0xFD, 0x00};
  send_command_(save_cmd);
  delay(100);
}

void HLK_LD2402::dump_config() {
  ESP_LOGCONFIG(TAG, "HLK-LD2402:");
  ESP_LOGCONFIG(TAG, "  Max Distance: %.1f m", max_distance_);
  ESP_LOGCONFIG(TAG, "  Disappear Delay: %u s", disappear_delay_);
  LOG_UPDATE_INTERVAL(this);
}