#include "hlk_ld2402.h"

namespace esphome {
namespace hlk_ld2402 {

void HLKLD2402Component::setup() {
  ESP_LOGCONFIG(TAG, "Setting up HLK-LD2402...");
}

void HLKLD2402Component::loop() {
  while (available()) {
    uint8_t c;
    read_byte(&c);
    
    if (c == '\n') {
      // Process complete line
      if (!line_buffer_.empty()) {
        process_line_(line_buffer_);
        line_buffer_.clear();
      }
    } else if (c != '\r') {  // Skip \r
      line_buffer_ += (char)c;
    }
  }
}

void HLKLD2402Component::process_line_(const std::string &line) {
  ESP_LOGV(TAG, "Got line: %s", line.c_str());
  
  if (line.compare(0, 9, "distance:") == 0) {
    // Extract distance value
    std::string distance_str = line.substr(9);
    char *end;
    float distance = strtof(distance_str.c_str(), &end);
    
    if (end != distance_str.c_str() && *end == '\0') {  // Successful conversion
      if (this->distance_sensor_ != nullptr) {
        if (!distance_in_cm_) {
          distance = distance / 100.0f;  // Convert to meters if needed
        }
        this->distance_sensor_->publish_state(distance);
      }
      ESP_LOGD(TAG, "Distance: %.2f %s", distance, distance_in_cm_ ? "cm" : "m");
    } else {
      ESP_LOGW(TAG, "Invalid distance value: %s", distance_str.c_str());
    }
  }
}

void HLKLD2402Component::dump_config() {
  ESP_LOGCONFIG(TAG, "HLK-LD2402:");
  ESP_LOGCONFIG(TAG, "  Distance unit: %s", distance_in_cm_ ? "cm" : "m");
}

}  // namespace hlk_ld2402
}  // namespace esphome