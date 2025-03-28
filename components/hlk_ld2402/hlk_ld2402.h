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
  
  void set_distance_sensor(sensor::Sensor *distance_sensor) { distance_sensor_ = distance_sensor; }
  void set_distance_in_cm(bool in_cm) { distance_in_cm_ = in_cm; }

 protected:
  std::string line_buffer_;
  void process_line_(const std::string &line);
  sensor::Sensor *distance_sensor_{nullptr};
  bool distance_in_cm_{true};  // Default to cm as per datasheet
};

}  // namespace hlk_ld2402
}  // namespace esphome