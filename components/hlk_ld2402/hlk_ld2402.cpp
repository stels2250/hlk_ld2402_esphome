#include "hlk_ld2402.h"
#include "esphome/core/log.h"

namespace esphome {
namespace hlk_ld2402 {

static const char *const TAG = "hlk_ld2402";

void HLKLD2402Component::setup() {
  ESP_LOGCONFIG(TAG, "Setting up HLK-LD2402...");
  
  // Enable configuration mode
  if (!this->enable_configuration_()) {
    ESP_LOGE(TAG, "Failed to enable configuration mode");
    this->mark_failed();
    return;
  }

  // Set work mode to engineering mode (0x04)
  if (!this->set_work_mode_(0x04)) {
    ESP_LOGE(TAG, "Failed to set work mode");
    this->mark_failed();
    return;
  }

  // Set max distance parameter
  std::vector<uint8_t> distance_data = {
      0x01, 0x00,  // Parameter ID for max distance
      static_cast<uint8_t>(this->max_distance_ * 10), 0x00, 0x00, 0x00  // Value (multiply by 10 for fixed-point)
  };
  if (!this->send_command_(0x0007, distance_data)) {
    ESP_LOGE(TAG, "Failed to set max distance");
    this->mark_failed();
    return;
  }

  // Set disappear delay parameter
  std::vector<uint8_t> delay_data = {
      0x04, 0x00,  // Parameter ID for disappear delay
      static_cast<uint8_t>(this->disappear_delay_ & 0xFF),
      static_cast<uint8_t>((this->disappear_delay_ >> 8) & 0xFF),
      0x00, 0x00
  };
  if (!this->send_command_(0x0007, delay_data)) {
    ESP_LOGE(TAG, "Failed to set disappear delay");
    this->mark_failed();
    return;
  }

  // Save parameters
  if (!this->send_command_(0x00FD)) {
    ESP_LOGE(TAG, "Failed to save parameters");
    this->mark_failed();
    return;
  }

  // Disable configuration mode
  if (!this->disable_configuration_()) {
    ESP_LOGE(TAG, "Failed to disable configuration mode");
    this->mark_failed();
    return;
  }

  if (this->distance_sensor_) this->distance_sensor_->publish_state(NAN);
  if (this->presence_sensor_) this->presence_sensor_->publish_state(false);
  if (this->movement_sensor_) this->movement_sensor_->publish_state(false);
  if (this->micromovement_sensor_) this->micromovement_sensor_->publish_state(false);
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
  
  return this->write_array(frame);
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
  float distance = (data[7] | (data[8] << 8)) / 100.0f;  // Convert to meters

  if (this->distance_sensor_ && !std::isnan(distance))
    this->distance_sensor_->publish_state(distance);
  if (this->presence_sensor_)
    this->presence_sensor_->publish_state(presence);
  if (this->movement_sensor_)
    this->movement_sensor_->publish_state(movement);
  if (this->micromovement_sensor_)
    this->micromovement_sensor_->publish_state(micromovement);

  ESP_LOGD(TAG, "State: %d, Distance: %.2fm", state, distance);
}

bool HLKLD2402Component::enable_configuration_() {
  std::vector<uint8_t> data = {0x01, 0x00};
  if (!this->send_command_(0x00FF, data))
    return false;
  this->configuration_mode_ = true;
  return true;
}

bool HLKLD2402Component::disable_configuration_() {
  if (!this->send_command_(0x00FE))
    return false;
  this->configuration_mode_ = false;
  return true;
}

bool HLKLD2402Component::set_work_mode_(uint32_t mode) {
  std::vector<uint8_t> data = {
      0x00, 0x00,  // Command value
      static_cast<uint8_t>(mode & 0xFF),
      static_cast<uint8_t>((mode >> 8) & 0xFF),
      static_cast<uint8_t>((mode >> 16) & 0xFF),
      static_cast<uint8_t>((mode >> 24) & 0xFF)
  };
  return this->send_command_(0x0012, data);
}

void HLKLD2402Component::dump_config() {
  ESP_LOGCONFIG(TAG, "HLK-LD2402:");
  ESP_LOGCONFIG(TAG, "  Max Distance: %.1f m", this->max_distance_);
  ESP_LOGCONFIG(TAG, "  Disappear Delay: %u s", this->disappear_delay_);
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