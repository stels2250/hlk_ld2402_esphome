#include "hlk_ld2402.h"
#include "esphome/core/log.h"

namespace esphome {
namespace hlk_ld2402 {

static const char *const TAG = "hlk_ld2402";

// Protocol constants
static const uint8_t FRAME_HEADER[4] = {0xFD, 0xFC, 0xFB, 0xFA};
static const uint8_t FRAME_FOOTER[4] = {0x04, 0x03, 0x02, 0x01};
static const uint8_t DATA_HEADER[4] = {0xF4, 0xF3, 0xF2, 0xF1};
static const uint8_t DATA_FOOTER[4] = {0xF8, 0xF7, 0xF6, 0xF5};

void HLKLD2402Component::setup() {
  ESP_LOGCONFIG(TAG, "Setting up HLK-LD2402...");
  
  // Initialize sensor states
  if (this->presence_sensor_ != nullptr) {
    this->presence_sensor_->publish_state(false);
  }
  if (this->distance_sensor_ != nullptr) {
    this->distance_sensor_->publish_state(NAN);
  }
  if (this->movement_sensor_ != nullptr) {
    this->movement_sensor_->publish_state(false);
  }
  if (this->micromovement_sensor_ != nullptr) {
    this->micromovement_sensor_->publish_state(false);
  }
}

void HLKLD2402Component::loop() {
  // Read available data
  while (this->available()) {
    uint8_t c;
    this->read_byte(&c);
    this->buffer_.push_back(c);

    // Check for data frame footer
    if (this->buffer_.size() >= 4) {
      size_t footer_pos = this->buffer_.size() - 4;
      if (memcmp(&this->buffer_[footer_pos], DATA_FOOTER, 4) == 0) {
        this->process_data_(this->buffer_);
        this->buffer_.clear();
        continue;
      }
    }

    // Prevent buffer overflow
    if (this->buffer_.size() > 128) {
      ESP_LOGW(TAG, "Buffer overflow, clearing buffer");
      this->buffer_.clear();
    }
  }
}

void HLKLD2402Component::process_data_(const std::vector<uint8_t> &data) {
  if (data.size() < 8) {
    ESP_LOGW(TAG, "Data frame too short (%d bytes)", data.size());
    return;
  }

  // Verify header
  if (memcmp(&data[0], DATA_HEADER, 4) != 0) {
    ESP_LOGW(TAG, "Invalid data frame header");
    return;
  }

  // Get frame length (little endian)
  uint16_t frame_len = data[4] | (data[5] << 8);
  if (data.size() < 8 + frame_len) {
    ESP_LOGW(TAG, "Incomplete data frame");
    return;
  }

  // Parse detection result
  uint8_t detection_state = data[6];
  bool presence = (detection_state == 0x01 || detection_state == 0x02);
  bool movement = (detection_state == 0x01);  // 0x01 = moving, 0x02 = stationary
  bool micromovement = (detection_state