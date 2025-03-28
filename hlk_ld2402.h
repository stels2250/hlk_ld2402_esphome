#pragma once

#include "esphome.h"

class HLK_LD2402 : public Component, public UARTDevice {
 public:
  HLK_LD2402(UARTComponent *parent) : UARTDevice(parent) {}

  void setup() override;
  void loop() override;
  void dump_config() override;

  // Configuration parameters
  void set_max_distance(float distance) { max_distance_ = distance; }
  void set_disappear_delay(uint16_t delay) { disappear_delay_ = delay; }

  // Sensors
  BinarySensor *presence_sensor{nullptr};
  Sensor *distance_sensor{nullptr};
  BinarySensor *movement_sensor{nullptr};
  BinarySensor *micromovement_sensor{nullptr};

 protected:
  void process_data_(const std::vector<uint8_t> &data);
  void send_command_(const std::vector<uint8_t> &command);
  void enable_config_mode_();
  void disable_config_mode_();
  void read_firmware_version_();
  void configure_sensor_();

  float max_distance_{8.5f};  // meters
  uint16_t disappear_delay_{30};  // seconds
  bool config_mode_{false};
  bool initialized_{false};
  uint32_t last_send_{0};
  std::vector<uint8_t> buffer_;
};