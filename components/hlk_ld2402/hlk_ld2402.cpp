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
        this->distance_sensor_->publish_state(distance / 100.0f);  // Convert to meters
      }
      ESP_LOGD(TAG, "Distance: %.2f m", distance / 100.0f);
    } else {
      ESP_LOGW(TAG, "Invalid distance value: %s", distance_str.c_str());
    }
  }
}

void HLKLD2402Component::dump_config() {
  ESP_LOGCONFIG(TAG, "HLK-LD2402:");
}

void HLKLD2402Component::dump_hex(const std::vector<uint8_t> &data, const char* prefix) {
  std::string hex;
  for (uint8_t b : data) {
    char buf[4];
    sprintf(buf, "%02X ", b);
    hex += buf;
  }
  ESP_LOGI(TAG, "%s: %s", prefix, hex.c_str());
}

void HLKLD2402Component::test_raw_tx(const std::vector<uint8_t> &data) {
  ESP_LOGI(TAG, "Sending raw data (%d bytes):", data.size());
  dump_hex(data, "TX");
  write_array(data.data(), data.size());
}

void HLKLD2402Component::test_get_version() {
  ESP_LOGI(TAG, "Testing GET_VERSION command");
  
  std::vector<uint8_t> frame;
  frame.insert(frame.end(), {0xFD, 0xFC, 0xFB, 0xFA});  // Header
  frame.insert(frame.end(), {0x02, 0x00});              // Length
  frame.insert(frame.end(), {0x00, 0x00});              // Command
  frame.insert(frame.end(), {0x04, 0x03, 0x02, 0x01});  // Footer
  
  test_raw_tx(frame);
}

}  // namespace hlk_ld2402
}  // namespace esphome