#include "hlk_ld2402.h"
#include "esphome/core/log.h"

namespace esphome {
namespace hlk_ld2402 {

void HLKLD2402Component::setup() {
  ESP_LOGCONFIG(TAG, "Setting up HLK-LD2402...");
}

void HLKLD2402Component::loop() {
  while (available()) {
    uint8_t c;
    read_byte(&c);
    this->buffer_.push_back(c);
    
    // Log each byte we receive
    ESP_LOGV(TAG, "RX: 0x%02X", c);
    
    // Keep buffer size reasonable
    if (this->buffer_.size() > 128) {
      this->buffer_.clear();
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
  
  this->write_array(data.data(), data.size());
}

void HLKLD2402Component::test_get_version() {
  ESP_LOGI(TAG, "Testing GET_VERSION command");
  
  std::vector<uint8_t> frame;
  // Header
  frame.insert(frame.end(), FRAME_HEADER, FRAME_HEADER + 4);
  // Length (2 bytes for command)
  frame.push_back(0x02);
  frame.push_back(0x00);
  // Command (GET_VERSION)
  frame.push_back(0x00);
  frame.push_back(0x00);
  // Footer
  frame.insert(frame.end(), FRAME_FOOTER, FRAME_FOOTER + 4);
  
  test_raw_tx(frame);
}

}  // namespace hlk_ld2402
}  // namespace esphome