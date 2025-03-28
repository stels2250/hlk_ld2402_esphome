#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/core/log.h"  // Add this include

namespace esphome {
namespace hlk_ld2402 {

static const char *const TAG = "hlk_ld2402";  // Moved TAG here

// Frame constants
static const uint8_t FRAME_HEADER[4] = {0xFD, 0xFC, 0xFB, 0xFA};
static const uint8_t FRAME_FOOTER[4] = {0x04, 0x03, 0x02, 0x01};

// Command constants
static const uint16_t CMD_GET_VERSION = 0x0000;
static const uint16_t CMD_ENABLE_CONFIG = 0x00FF;
static const uint16_t CMD_DISABLE_CONFIG = 0x00FE;

class HLKLD2402Component : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  // Test interface
  void test_get_version();
  void test_raw_tx(const std::vector<uint8_t> &data);
  void dump_hex(const std::vector<uint8_t> &data, const char* prefix);

 protected:
  std::vector<uint8_t> buffer_;
};

}  // namespace hlk_ld2402
}  // namespace esphome