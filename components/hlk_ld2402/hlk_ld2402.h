#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"

namespace esphome {
namespace hlk_ld2402 {

static const uint8_t FRAME_HEADER[] = {0xFD, 0xFC, 0xFB, 0xFA};
static const uint8_t FRAME_FOOTER[] = {0x04, 0x03, 0x02, 0x01};

// Commands
static const uint16_t CMD_ENABLE_CONFIG = 0x00FF;
static const uint16_t CMD_DISABLE_CONFIG = 0x00FE;
static const uint16_t CMD_GET_VERSION = 0x0001;
static const uint16_t CMD_SET_PARAMS = 0x0002;
static const uint16_t CMD_GET_PARAMS = 0x0003;
static const uint16_t CMD_SET_MODE = 0x0004;
static const uint16_t CMD_SAVE_PARAMS = 0x0005;
static const uint16_t CMD_START_CALIBRATION = 0x000A;
static const uint16_t CMD_GET_CALIBRATION_STATUS = 0x000B;
static const uint16_t CMD_AUTO_GAIN = 0x000C;

// Parameters
static const uint16_t PARAM_MAX_DISTANCE = 0x0001;
static const uint16_t PARAM_TIMEOUT = 0x0004;

// Work modes
static const uint32_t MODE_NORMAL = 0x00000000;
static const uint32_t MODE_CONFIG = 0x00000001;

class HLKLD2402Component : public Component, public uart::UARTDevice {
public:
  void set_distance_sensor(sensor::Sensor *distance_sensor) { distance_sensor_ = distance_sensor; }
  void set_presence_binary_sensor(binary_sensor::BinarySensor *presence) { presence_binary_sensor_ = presence; }
  void set_max_distance(float max_distance) { max_distance_ = max_distance; }
  void set_timeout(uint32_t timeout) { timeout_ = timeout; }
  
  void setup() override;
  void loop() override;
  void dump_config() override;
  
  void calibrate();
  void save_config();
  void enable_auto_gain();

protected:
  bool enter_config_mode_();
  bool exit_config_mode_();
  bool send_command_(uint16_t command, const uint8_t *data = nullptr, size_t len = 0);
  bool read_response_(std::vector<uint8_t> &response);
  bool set_parameter_(uint16_t param_id, uint32_t value);
  bool get_parameter_(uint16_t param_id, uint32_t &value);
  bool set_work_mode_(uint32_t mode);
  void process_line_(const std::string &line);
  void dump_hex_(const uint8_t *data, size_t len, const char* prefix);
  
  sensor::Sensor *distance_sensor_{nullptr};
  binary_sensor::BinarySensor *presence_binary_sensor_{nullptr};
  float max_distance_{5.0};
  uint32_t timeout_{5};
  bool config_mode_{false};
  std::string firmware_version_;
  std::string line_buffer_;
};

}  // namespace hlk_ld2402
}  // namespace esphome