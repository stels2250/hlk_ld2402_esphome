#include "hlk_ld2402.h"
#include "esphome/core/log.h"
#include <string>

namespace esphome {
namespace hlk_ld2402 {

static const char *const TAG = "hlk_ld2402";

void HLKLD2402Component::setup() {
  ESP_LOGCONFIG(TAG, "Setting up HLK-LD2402...");

  // Pre-setup validation
  if (!this->available()) {
    ESP_LOGE(TAG, "UART not configured");
    this->mark_failed();
    return;
  }

  // Log initial configuration
  ESP_LOGD(TAG, "Initial config - Max Distance: %.1f m, Disappear Delay: %u s", 
           this->max_distance_, this->disappear_delay_);
  
  // Clear any stale data
  this->buffer_.clear();
  while (this->available()) {
    uint8_t c;
    this->read_byte(&c);
    ESP_LOGV(TAG, "Cleared stale byte: 0x%02X", c);
  }
  
  // Wait for sensor to be ready after power-up
  ESP_LOGD(TAG, "Waiting for sensor initialization...");
  delay(500);

  // Try to get version first to verify communication
  ESP_LOGD(TAG, "Checking sensor version...");
  std::vector<uint8_t> version_data;
  if (!this->send_command_(CMD_GET_VERSION, version_data)) {
    ESP_LOGE(TAG, "Failed to get version - no response from sensor");
    this->mark_failed();
    return;
  }
  
  ESP_LOGD(TAG, "Enabling configuration mode...");
  for (int retry = 0; retry < 3; retry++) {
    if (this->enable_configuration_()) {
      ESP_LOGD(TAG, "Configuration mode enabled on attempt %d", retry + 1);
      ESP_LOGD(TAG, "Protocol Version: 0x%04X, Buffer Size: 0x%04X", 
               this->protocol_version_, this->buffer_size_);
      break;
    }
    if (retry == 2) {
      ESP_LOGE(TAG, "Failed to enable configuration mode after 3 attempts");
      this->mark_failed();
      return;
    }
    ESP_LOGW(TAG, "Retrying configuration mode enable...");
    delay(100);
  }

  delay(100);

  ESP_LOGD(TAG, "Starting auto gain calibration...");
  if (!this->auto_gain_calibration_()) {
    ESP_LOGW(TAG, "Auto gain calibration failed or not supported - continuing anyway");
  } else {
    ESP_LOGD(TAG, "Auto gain calibration successful");
    delay(100);
  }

  ESP_LOGD(TAG, "Setting engineering mode...");
  if (!this->set_work_mode_(true)) {
    ESP_LOGE(TAG, "Failed to set engineering mode");
    this->mark_failed();
    return;
  }
  ESP_LOGD(TAG, "Engineering mode set successfully");

  delay(100);

  // Set max distance (converted to cm)
  uint16_t distance_cm = static_cast<uint16_t>(this->max_distance_ * 100);
  ESP_LOGD(TAG, "Setting max distance to %u cm...", distance_cm);
  if (!this->set_parameter_(PARAM_MAX_DISTANCE, distance_cm)) {
    ESP_LOGE(TAG, "Failed to set max distance");
    this->mark_failed();
    return;
  }
  ESP_LOGD(TAG, "Max distance set successfully");

  delay(100);

  ESP_LOGD(TAG, "Setting disappear delay to %u s...", this->disappear_delay_);
  if (!this->set_parameter_(PARAM_DISAPPEAR_DELAY, this->disappear_delay_)) {
    ESP_LOGE(TAG, "Failed to set disappear delay");
    this->mark_failed();
    return;
  }
  ESP_LOGD(TAG, "Disappear delay set successfully");

  delay(100);

  ESP_LOGD(TAG, "Saving configuration...");
  if (!this->save_configuration_()) {
    ESP_LOGE(TAG, "Failed to save configuration");
    this->mark_failed();
    return;
  }
  ESP_LOGD(TAG, "Configuration saved successfully");

  delay(100);

  ESP_LOGD(TAG, "Disabling configuration mode...");
  if (!this->disable_configuration_()) {
    ESP_LOGE(TAG, "Failed to disable configuration mode");
    this->mark_failed();
    return;
  }
  ESP_LOGD(TAG, "Configuration mode disabled");

  // Initialize sensor states
  if (this->distance_sensor_) this->distance_sensor_->publish_state(NAN);
  if (this->presence_sensor_) this->presence_sensor_->publish_state(false);
  if (this->movement_sensor_) this->movement_sensor_->publish_state(false);
  if (this->micromovement_sensor_) this->micromovement_sensor_->publish_state(false);

  ESP_LOGCONFIG(TAG, "HLK-LD2402 setup complete");
}

void HLKLD2402Component::loop() {
  while (available()) {
    uint8_t c;
    read_byte(&c);
    this->buffer_.push_back(c);

    // Check if we have a complete frame
    if (this->buffer_.size() >= 8) {
      if (this->check_data_footer_(this->buffer_, this->buffer_.size() - 4)) {
        this->process_data_(this->buffer_);
        this->buffer_.clear();
      }
    }

    // Prevent buffer overflow
    if (this->buffer_.size() > 128)
      this->buffer_.clear();
  }
}

bool HLKLD2402Component::send_command_(uint16_t command, const std::vector<uint8_t> &data) {
  std::vector<uint8_t> frame;
  
  // Add frame header
  frame.insert(frame.end(), FRAME_HEADER, FRAME_HEADER + 4);
  
  // Add data length (2 bytes)
  uint16_t length = data.size() + 2;  // +2 for command
  frame.push_back(length & 0xFF);
  frame.push_back((length >> 8) & 0xFF);
  
  // Add command (2 bytes)
  frame.push_back(command & 0xFF);
  frame.push_back((command >> 8) & 0xFF);
  
  // Add data
  frame.insert(frame.end(), data.begin(), data.end());
  
  // Add frame footer
  frame.insert(frame.end(), FRAME_FOOTER, FRAME_FOOTER + 4);
  
  // Validate frame length
  if (frame.size() < 12) {  // Minimum frame size: 4 (header) + 2 (length) + 2 (command) + 4 (footer)
    ESP_LOGE(TAG, "Invalid frame length: %d", frame.size());
    return false;
  }
  
  // Clear any existing data in buffer
  this->buffer_.clear();
  this->last_command_ = command;
  this->last_command_success_ = false;
  
  // Log the frame being sent
  std::string frame_hex;
  for (uint8_t b : frame) {
    char hex[3];
    sprintf(hex, "%02X", b);
    frame_hex += hex;
    frame_hex += " ";
  }
  ESP_LOGD(TAG, "Sending frame (%d bytes): %s", frame.size(), frame_hex.c_str());
  
  // Send the frame
  this->write_array(frame.data(), frame.size());
  
  return this->wait_for_response_(command);
}

bool HLKLD2402Component::wait_for_response_(uint16_t command, uint32_t timeout_ms) {
  uint32_t start = millis();
  std::string response_hex;
  
  while ((millis() - start) < timeout_ms) {
    if (available()) {
      uint8_t c;
      read_byte(&c);
      this->buffer_.push_back(c);
      
      char hex[3];
      sprintf(hex, "%02X", c);
      response_hex += hex;
      response_hex += " ";

      // Check if we have a complete ACK frame
      if (this->buffer_.size() >= 8 && 
          this->check_frame_header_(this->buffer_) &&
          this->check_frame_footer_(this->buffer_, this->buffer_.size() - 4)) {
        
        ESP_LOGV(TAG, "Received response: %s", response_hex.c_str());
        
        // Parse ACK frame
        if (this->buffer_.size() >= 6) {
          uint16_t resp_command = this->buffer_[4] | (this->buffer_[5] << 8);
          if (resp_command == command) {
            // Special handling for enable configuration command
            if (command == CMD_ENABLE_CONFIG && this->buffer_.size() >= 12) {
              uint16_t status = this->buffer_[6] | (this->buffer_[7] << 8);
              this->protocol_version_ = this->buffer_[8] | (this->buffer_[9] << 8);
              this->buffer_size_ = this->buffer_[10] | (this->buffer_[11] << 8);
              ESP_LOGV(TAG, "Enable config: status=%04X, proto=%04X, buf=%04X", 
                      status, this->protocol_version_, this->buffer_size_);
              this->last_command_success_ = (status == 0);
              return this->last_command_success_;
            } else if (this->buffer_.size() >= 8) {
              uint16_t status = this->buffer_[6] | (this->buffer_[7] << 8);
              this->last_command_success_ = (status == 0);
              ESP_LOGV(TAG, "Command 0x%04X %s (status: 0x%04X)", 
                      command, this->last_command_success_ ? "succeeded" : "failed", status);
              return this->last_command_success_;
            }
          } else {
            ESP_LOGW(TAG, "Response command mismatch. Expected: 0x%04X, Got: 0x%04X", 
                    command, resp_command);
          }
        }
        break;  // Exit if we got any complete frame
      }
    }
    delay(1);
  }
  
  if (response_hex.empty()) {
    ESP_LOGW(TAG, "Command 0x%04X timeout - no response received", command);
  } else {
    ESP_LOGW(TAG, "Command 0x%04X timeout - incomplete response: %s", 
             command, response_hex.c_str());
  }
  return false;
}

void HLKLD2402Component::process_data_(const std::vector<uint8_t> &data) {
  if (data.size() < 8 || !this->check_data_header_(data))
    return;

  uint16_t frame_len = data[4] | (data[5] << 8);
  if (data.size() < 8 + frame_len)
    return;

  uint8_t state = data[6];
  bool presence = (state == 0x01 || state == 0x02);
  bool movement = (state == 0x01);
  bool micromovement = (state == 0x02);
  
  uint16_t distance_cm = data[7] | (data[8] << 8);
  float distance_m = distance_cm / 100.0f;

  ESP_LOGV(TAG, "Raw data: state=%02X, distance=%u cm", state, distance_cm);

  if (this->distance_sensor_ && !std::isnan(distance_m)) {
    this->distance_sensor_->publish_state(distance_m);
  }
  if (this->presence_sensor_) {
    this->presence_sensor_->publish_state(presence);
  }
  if (this->movement_sensor_) {
    this->movement_sensor_->publish_state(movement);
  }
  if (this->micromovement_sensor_) {
    this->micromovement_sensor_->publish_state(micromovement);
  }

  ESP_LOGD(TAG, "State: %d, Distance: %.2fm", state, distance_m);
}

bool HLKLD2402Component::enable_configuration_() {
  std::vector<uint8_t> data = {0x01, 0x00};
  if (!this->send_command_(CMD_ENABLE_CONFIG, data))
    return false;
  this->configuration_mode_ = true;
  return true;
}

bool HLKLD2402Component::disable_configuration_() {
  if (!this->send_command_(CMD_DISABLE_CONFIG))
    return false;
  this->configuration_mode_ = false;
  return true;
}

bool HLKLD2402Component::set_work_mode_(bool engineering_mode) {
  std::vector<uint8_t> data = {
      0x00, 0x00,  // Command value is always 0x0000
      engineering_mode ? 0x04 : 0x64, 0x00, 0x00, 0x00  // 0x04000000 for engineering, 0x64000000 for normal
  };
  return this->send_command_(CMD_SET_MODE, data);
}

bool HLKLD2402Component::set_parameter_(uint16_t param_id, uint32_t value) {
  std::vector<uint8_t> data = {
      static_cast<uint8_t>(param_id & 0xFF),
      static_cast<uint8_t>((param_id >> 8) & 0xFF),
      static_cast<uint8_t>(value & 0xFF),
      static_cast<uint8_t>((value >> 8) & 0xFF),
      static_cast<uint8_t>((value >> 16) & 0xFF),
      static_cast<uint8_t>((value >> 24) & 0xFF)
  };
  return this->send_command_(CMD_SET_PARAMS, data);
}

bool HLKLD2402Component::save_configuration_() {
  return this->send_command_(CMD_SAVE_PARAMS);
}

bool HLKLD2402Component::auto_gain_calibration_() {
  return this->send_command_(CMD_AUTO_GAIN);
}

void HLKLD2402Component::dump_config() {
  ESP_LOGCONFIG(TAG, "HLK-LD2402:");
  ESP_LOGCONFIG(TAG, "  Max Distance: %.1f m", this->max_distance_);
  ESP_LOGCONFIG(TAG, "  Disappear Delay: %u s", this->disappear_delay_);
  ESP_LOGCONFIG(TAG, "  Protocol Version: 0x%04X", this->protocol_version_);
  ESP_LOGCONFIG(TAG, "  Buffer Size: 0x%04X", this->buffer_size_);
  LOG_SENSOR("  ", "Distance", this->distance_sensor_);
  LOG_BINARY_SENSOR("  ", "Presence", this->presence_sensor_);
  LOG_BINARY_SENSOR("  ", "Movement", this->movement_sensor_);
  LOG_BINARY_SENSOR("  ", "Micro-movement", this->micromovement_sensor_);
}

void HLKLD2402DistanceSensor::setup() {
  if (this->parent_ == nullptr) {
    ESP_LOGE(TAG, "Parent not set for distance sensor!");
    this->mark_failed();
    return;
  }
  this->parent_->set_distance_sensor(this);
}

void HLKLD2402BinarySensor::setup() {
  if (this->parent_ == nullptr) {
    ESP_LOGE(TAG, "Parent not set for binary sensor!");
    this->mark_failed();
    return;
  }
  if (this->type_ == "presence") {
    this->parent_->set_presence_sensor(this);
  } else if (this->type_ == "movement") {
    this->parent_->set_movement_sensor(this);
  } else if (this->type_ == "micromovement") {
    this->parent_->set_micromovement_sensor(this);
  }
}

}  // namespace hlk_ld2402
}  // namespace esphome