#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/log.h"

namespace esphome {
namespace hlk_ld2402 {

static const char *const TAG = "hlk_ld2402";

class HLKLD2402Component : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  
  void test_get_version();
  void test_raw_tx(const std::vector<uint8_t> &data);
  void dump_hex(const std::vector<uint8_t> &data, const char* prefix);

  void set_distance_sensor(sensor::Sensor *distance_sensor) { distance_sensor_ = distance_sensor; }

 protected:
  std::vector<uint8_t> buffer_;
  std::string line_buffer_;
  void process_line_(const std::string &line);
  sensor::Sensor *distance_sensor_{nullptr};
};

}  // namespace hlk_ld2402
}  // namespace esphome