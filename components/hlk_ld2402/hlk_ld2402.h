#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/core/log.h"

namespace esphome {
namespace hlk_ld2402 {

static const char *const TAG = "hlk_ld2402";

// Frame constants (from section 5.1.2)
static const uint8_t FRAME_HEADER[4] = {0xFD, 0xFC, 0xFB, 0xFA};
static const uint8_t FRAME_FOOTER[4] = {0x04, 0x03, 0x02, 0x01};

// Command constants (from section 5.2)
static const uint16_t CMD_GET_VERSION = 0x0000;
static const uint16_t CMD_ENABLE_CONFIG = 0x00FF;
static const uint16_t CMD_DISABLE_CONFIG = 0x00FE;
static const uint16_t CMD_GET_PARAMS = 0x0008;
static const uint16_t CMD_SET_PARAMS = 0x0007;
static const uint16_t CMD_SET_MODE = 0x0012;
static const uint16_t CMD_START_CALIBRATION = 0x0009;
static const uint16_t CMD_GET_CALIBRATION_STATUS = 0x000A;
static const uint16_t CMD_SAVE_PARAMS = 0x00FD;
static const uint16_t CMD_AUTO_GAIN = 0x00EE;

// Parameter IDs (from section 5.2.7)
static const uint16_t PARAM_MAX_DISTANCE = 0x0001;     // 7-100 (0.7m - 10m)
static const uint16_t PARAM_TIMEOUT = 0x0004;          // 0-65535 seconds
static const uint16_t PARAM_POWER_INTERFERENCE = 0x0005;  // Read only
static const uint16_t PARAM_MOTION_THRESHOLD = 0x0010;  // Start of motion thresholds
static const uint16_t PARAM_MICROMOTION_THRESHOLD = 0x0030;  // Start of micromotion thresholds

// Specification constants (from section 2)
static const float MAX_RANGE_WALL = 10.0f;  // meters, wall mount
static const float MAX_RANGE_CEILING = 5.0f;  // meters, ceiling mount
static const float DETECTION_ACCURACY = 0.15f;  // meters, for moving targets within 6m
static const uint32_t DATA_REFRESH_PERIOD = 165;  // milliseconds

class HLKLD2402Component : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }
  
  void set_distance_sensor(sensor::Sensor *distance_sensor) { distance_sensor_ = distance_sensor; }
  void set_presence_binary_sensor(binary_sensor::BinarySensor *presence) { presence_binary_sensor_ = presence; }
  void set_max_distance_m(float distance) { max_distance_ = distance * 10; }  // Convert m to dm
  void set_timeout(uint16_t timeout) { timeout_ = timeout; }

  // Control methods
  void calibrate();
  void save_config();
  void enable_auto_gain();
  
 protected:
  std::string line_buffer_;
  void process_line_(const std::string &line);
  
  // Sensors
  sensor::Sensor *distance_sensor_{nullptr};
  binary_sensor::BinarySensor *presence_binary_sensor_{nullptr};
  
  // Configuration
  uint16_t max_distance_{100};  // in decimeters (10m default)
  uint16_t timeout_{5};  // 5 seconds default
  
  // Helper methods for command protocol
  bool send_command_(uint16_t command, const uint8_t *data = nullptr, size_t len = 0);
  bool read_response_(std::vector<uint8_t> &response);
  void dump_hex_(const uint8_t *data, size_t len, const char* prefix);
  bool enter_config_mode_();
  bool exit_config_mode_();
  bool set_parameter_(uint16_t param_id, uint32_t value);
  bool get_parameter_(uint16_t param_id, uint32_t &value);

  // State tracking
  uint32_t last_update_{0};
  bool config_mode_{false};
  std::string firmware_version_;
};

}  // namespace hlk_ld2402
}  // namespace esphome