#include "hlk_ld2402.h"
#include "esphome/core/log.h"
#include <string>

namespace esphome {
namespace hlk_ld2402 {

static const char *const TAG = "hlk_ld2402";

bool HLKLD2402Component::verify_uart_() const {
  if (!this->get_uart()) {
    ESP_LOGE(TAG, "UART parent is not set!");
    return false;
  }
  if (!this->get_uart()->get_tx_pin()) {
    ESP_LOGE(TAG, "UART TX pin is not set!");
    return false;
  }
  if (!this->get_uart()->get_rx_pin()) {
    ESP_LOGE(TAG, "UART RX pin is not set!");
    return false;
  }
  return true;
}

void HLKLD2402Component::clear_rx_buffer_() {
  uint8_t c;
  while (available()) {
    read_byte(&c);
    ESP_LOGV(TAG, "Cleared stale byte: 0x%02X", c);
  }
}

void HLKLD2402Component::setup() {
  ESP_LOGCONFIG(TAG, "Setting up HLK-LD2402...");

  if (!this->verify_uart_()) {
    this->mark_failed();
    return;
  }

  ESP_LOGD(TAG, "UART Config - TX: %d, RX: %d, Baud: %d", 
           this->get_uart()->get_tx_pin()->get_pin(),
           this->get_uart()->get_rx_pin()->get_pin(),
           this->get_uart()->get_baud_rate());

  // Clear any stale data
  this->buffer_.clear();
  this->clear_rx_buffer_();
  
  // Wait for sensor to be ready after power-up
  ESP_LOGD(TAG, "Waiting for sensor initialization...");
  delay(1000);

  // Try to get version first
  ESP_LOGD(TAG, "Checking sensor version...");
  if (!this->send_command_(CMD_GET_VERSION)) {
    ESP_LOGE(TAG, "Failed to get version - no response from sensor");
    this->mark_failed();
    return;
  }
  
  ESP_LOGD(TAG, "Enabling configuration mode...");
  for (int retry = 0; retry < 3; retry++) {
    if (this->enable_configuration_()) {
      ESP_LOGD(TAG, "Configuration mode enabled on attempt %d", retry + 1);
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
  }

  delay(100);

  ESP_LOGD(TAG, "Setting engineering mode...");
  if (!this->set_work_mode_(true)) {
    ESP_LOGE(TAG, "Failed to set engineering mode");
    this->mark_failed();
    return;
  }

  delay(100);

  // Set max distance (converted to cm)
  uint16_t distance_cm = static_cast<uint16_t>(this->max_distance_ * 100);
  ESP_LOGD(TAG, "Setting max distance to %u cm...", distance_cm);
  if (!this->set_parameter_(PARAM_MAX_DISTANCE, distance_cm)) {
    ESP_LOGE(TAG, "Failed to set max distance");
    this->mark_failed();
    return;
  }

  delay(100);

  ESP_LOGD(TAG, "Setting disappear delay to %u s...", this->disappear_delay_);
  if (!this->set_parameter_(PARAM_DISAPPEAR_DELAY, this->disappear_delay_)) {
    ESP_LOGE(TAG, "Failed to set disappear delay");
    this->mark_failed();
    return;
  }

  delay(100);

  ESP_LOGD(TAG, "Saving configuration...");
  if (!this->save_configuration_()) {
    ESP_LOGE(TAG, "Failed to save configuration");
    this->mark_failed();
    return;
  }

  delay(100);

  ESP_LOGD(TAG, "Disabling configuration mode...");
  if (!this->disable_configuration_()) {
    ESP_LOGE(TAG, "Failed to disable configuration mode");
    this->mark_failed();
    return;
  }

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
    if (this->buffer_.size() > 128) {
      ESP_LOGW(TAG, "Buffer overflow, clearing");
      this->buffer_.clear();
    }
  }
}

bool HLKLD2402Component::send_command_(uint16_t command, const std::vector<uint8_t> &data) {
  if (!this->verify_uart_()) {
    return false;
  }

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
  if (frame.size() < 12) {
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
  ESP_LOGV(TAG, "Sending frame (%d bytes): %s", frame.size(), frame_hex.c_str());
  
  // Send the frame
  size_t written = this->write_array(frame);
  if (written != frame.size()) {
    ESP_LOGE(TAG, "Failed to write complete frame: wrote %d/%d bytes", written, frame.size());
    return false;
  }
  
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
        break;
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
  if (data.size() < 8 || !this->check_data_header_(data)) {
    ESP_LOGW(TAG, "Invalid data frame");
    return;
  }

  uint16_t frame_len = data[4] | (data[5] << 8);
  if (data.size() < 8 + frame_len) {
    ESP_LOGW(TAG, "Incomplete data frame");
    return;
  }

  uint8_t state = data[6];
  bool presence = (state == 0x01 || state == 0x02);
  bool movement = (state == 0x01);
  bool micromovement = (state == 0x02);
  
  uint16_t distance_cm = data[7] | (data[8] << 8);
  float distance_m = distance_cm / 100.0f;

  ESP_LOGV(TAG, "Raw data: state=%02X, distance=%u cm", state, distance_cm);

  if (this->distance_sensor_ && !std::isnan(distance_m))
    this->distance_sensor_->publish_state(distance_m);