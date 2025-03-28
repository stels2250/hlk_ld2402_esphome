#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"

namespace esphome {
namespace hlk_ld2402 {

static const uint8_t FRAME_HEADER[4] = {0xFD, 0xFC, 0xFB, 0xFA};
static const uint8_t FRAME_FOOTER[4] = {0x04, 0x03, 0x02, 0x01};
static const uint8_t DATA_HEADER[4] = {0xF4, 0xF3, 0xF2, 0xF1};
static const uint8_t DATA_FOOTER[4] = {0xF8, 0xF7, 0xF6, 0xF5};

// Command constants
static const uint16_t CMD_GET_VERSION = 0x0000;
static const uint16_t CMD_ENABLE_CONFIG = 0x00FF;
static const uint16_t CMD_DISABLE_CONFIG = 0x00FE;
static const uint16_t CMD_GET_SN_HEX = 0x0016;
static const uint16_t CMD_GET_SN_CHAR = 0x0011;
static const uint16_t CMD_READ_PARAMS = 0x0008;
static const uint16_t CMD_SET_PARAMS = 0x0007;
static const uint16_t CMD_SET_MODE = 0x0012;
static const uint16_t CMD_SAVE_PARAMS = 0x00FD;
static const uint16_t CMD_AUTO_GAIN = 0x00EE;

// Parameter IDs
static const uint16_t PARAM_MAX_DISTANCE = 0x0001;
static const uint16_t PARAM_DISAPPEAR_DELAY = 0x0004;

class HLKLD2402Component;  // Forward declaration

class HLKLD2402DistanceSensor : public sensor::Sensor, public Component {
 public:
  void set_parent(HLKLD2402Component *parent) { parent_ = parent; }
  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
 protected:
  HLKLD2402Component *parent_;
};

class HLKLD2402BinarySensor : public binary_sensor::BinarySensor, public Component {
 public:
  void set_parent(HLKLD2402Component *parent) { parent_ = parent; }
  void set_type(const std::string &type) { type_ = type; }
  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
 protected:
  HLKLD2402Component *parent_;
  std::string type_;
};

class HLKLD2402Component : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  void set_max_distance(float distance) { max_distance_ = distance; }
  void set_disappear_delay(uint16_t delay) { disappear_delay_ = delay; }

  void set_distance_sensor(sensor::Sensor *sensor) { distance_sensor_ = sensor; }
  void set_presence_sensor(binary_sensor::BinarySensor *sensor) { presence_sensor_ = sensor; }
  void set_movement_sensor(binary_sensor::BinarySensor *sensor) { movement_sensor_ = sensor; }
  void set_micromovement_sensor(binary_sensor::BinarySensor *sensor) { micromovement_sensor_ = sensor; }

 protected:
  bool send_command_(uint16_t command, const std::vector<uint8_t> &data = {});
  bool wait_for_response_(uint16_t command, uint32_t timeout_ms = 1000);
  void process_data_(const std::vector<uint8_t> &data);
  bool check_frame_header_(const std::vector<uint8_t> &data, size_t offset = 0) {
    return data.size() >= offset + 4 && memcmp(&data[offset], FRAME_HEADER, 4) == 0;
  }
  bool check_frame_footer_(const std::vector<uint8_t> &data, size_t offset = 0) {
    return data.size() >= offset + 4 && memcmp(&data[offset], FRAME_FOOTER, 4) == 0;
  }
  bool check_data_header_(const std::vector<uint8_t> &data, size_t offset = 0) {
    return data.size() >= offset + 4 && memcmp(&data[offset], DATA_HEADER, 4) == 0;
  }
  bool check_data_footer_(const std::vector<uint8_t> &data, size_t offset = 0) {
    return data.size() >= offset + 4 && memcmp(&data[offset], DATA_FOOTER, 4) == 0;
  }
  bool enable_configuration_();
  bool disable_configuration_();
  bool set_work_mode_(bool engineering_mode);
  bool save_configuration_();
  bool auto_gain_calibration_();
  bool set_parameter_(uint16_t param_id, uint32_t value);
  bool verify_uart_() const;
  void clear_rx_buffer_();

  float max_distance_{8.5f};
  uint16_t disappear_delay_{30};

  sensor::Sensor *distance_sensor_{nullptr};
  binary_sensor::BinarySensor *presence_sensor_{nullptr};
  binary_sensor::BinarySensor *movement_sensor_{nullptr};
  binary_sensor::BinarySensor *micromovement_sensor_{nullptr};
  std::vector<uint8_t> buffer_;
  bool configuration_mode_{false};
  bool last_command_success_{false};
  uint16_t last_command_{0};
  uint16_t protocol_version_{0};
  uint16_t buffer_size_{0};
};

}  // namespace hlk_ld2402
}  // namespace esphome