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

void HLKLD2402Component::setup() {
  ESP_LOGCONFIG(TAG, "Setting up HLK-LD2402...");
  if (distance_sensor_) distance_sensor_->publish_state(NAN);
  if (presence_sensor_) presence_sensor_->publish_state(false);
  if (movement_sensor_) movement_sensor_->publish_state(false);
  if (micromovement_sensor_) micromovement_sensor_->publish_state(false);
}

void HLKLD2402Component::loop() {
  while (available()) {
    uint8_t c;
    read_byte(&c);
    buffer_.push_back(c);

    if (buffer_.size() >= 8 && memcmp(&buffer_[buffer_.size()-4], DATA_FOOTER, 4) == 0) {
      process_data_(buffer_);
      buffer_.clear();
    }

    if (buffer_.size() > 128) buffer_.clear();
  }
}

void HLKLD2402Component::process_data_(const std::vector<uint8_t> &data) {
  if (data.size() < 8 || memcmp(&data[0], DATA_HEADER, 4) != 0) return;

  uint16_t frame_len = data[4] | (data[5] << 8);
  if (data.size() < 8 + frame_len) return;

  uint8_t state = data[6];
  bool presence = (state == 0x01 || state == 0x02);
  bool movement = (state == 0x01);
  bool micromovement = (state == 0x02);
  float distance = (data[7] | (data[8] << 8)) / 100.0f;

  if (distance_sensor_) distance_sensor_->publish_state(distance);
  if (presence_sensor_) presence_sensor_->publish_state(presence);
  if (movement_sensor_) movement_sensor_->publish_state(movement);
  if (micromovement_sensor_) micromovement_sensor_->publish_state(micromovement);

  ESP_LOGD(TAG, "State: %d, Distance: %.2fm", state, distance);
}

void HLKLD2402Component::dump_config() {
  ESP_LOGCONFIG(TAG, "HLK-LD2402:");
  ESP_LOGCONFIG(TAG, "  Max Distance: %.1f m", max_distance_);
  ESP_LOGCONFIG(TAG, "  Disappear Delay: %u s", disappear_delay_);
  LOG_SENSOR("  ", "Distance", distance_sensor_);
  LOG_BINARY_SENSOR("  ", "Presence", presence_sensor_);
  LOG_BINARY_SENSOR("  ", "Movement", movement_sensor_);
  LOG_BINARY_SENSOR("  ", "Micro-movement", micromovement_sensor_);
}

}  // namespace hlk_ld2402
}  // namespace esphome