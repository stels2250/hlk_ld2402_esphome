#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"  // Include without condition

namespace esphome {
namespace hlk_ld2402 {

static const uint8_t FRAME_HEADER[] = {0xFD, 0xFC, 0xFB, 0xFA};
static const uint8_t FRAME_FOOTER[] = {0x04, 0x03, 0x02, 0x01};

// Add new frame format constants
static const uint8_t DATA_FRAME_HEADER[] = {0xF4, 0xF3, 0xF2, 0xF1}; // Data frame header
static const uint8_t DATA_FRAME_TYPE_DISTANCE = 0x83; // Distance data frame type

// Commands
static const uint16_t CMD_GET_VERSION = 0x0000;  // Changed from 0x0001 to 0x0000
static const uint16_t CMD_ENABLE_CONFIG = 0x00FF;
static const uint16_t CMD_DISABLE_CONFIG = 0x00FE;
static const uint16_t CMD_GET_SN_HEX = 0x0016;   // Added - read SN in hex format
static const uint16_t CMD_GET_SN_CHAR = 0x0011;  // Added - read SN in character format
static const uint16_t CMD_GET_PARAMS = 0x0008;   // Changed from 0x0003
static const uint16_t CMD_SET_PARAMS = 0x0007;   // Changed from 0x0002
static const uint16_t CMD_SET_MODE = 0x0012;     // Changed from 0x0004
static const uint16_t CMD_SAVE_PARAMS = 0x00FD;  // Changed from 0x0005
static const uint16_t CMD_START_CALIBRATION = 0x0009;  // Changed from 0x000A
static const uint16_t CMD_GET_CALIBRATION_STATUS = 0x000A;  // Changed from 0x000B
static const uint16_t CMD_AUTO_GAIN = 0x00EE;    // Changed from 0x000C
static const uint16_t CMD_AUTO_GAIN_COMPLETE = 0x00F0; // Added - auto gain completion notice

// Parameters
static const uint16_t PARAM_MAX_DISTANCE = 0x0001;
static const uint16_t PARAM_TIMEOUT = 0x0004;

// Work modes
static const uint32_t MODE_NORMAL = 0x00000064;   // Changed from 0x00000000
static const uint32_t MODE_CONFIG = 0x00000001;
static const uint32_t MODE_ENGINEERING = 0x00000004;  // For testing

// Work modes from manual
static const uint32_t MODE_PRODUCTION = 0x00000064;   // Normal operation

// Thresholds from manual
static const uint16_t PARAM_TRIGGER_THRESHOLD = 0x0010;  // Range: 0x0010-0x001F
static const uint16_t PARAM_MICRO_THRESHOLD = 0x0030;    // Range: 0x0030-0x003F
static const uint16_t PARAM_POWER_INTERFERENCE = 0x0005; // Power interference status

// Update - Correct baud rate according to manual
static const uint32_t UART_BAUD_RATE = 115200;
static const uint8_t UART_STOP_BITS = 1;
static const uint8_t UART_DATA_BITS = 8;
static const esphome::uart::UARTParityOptions UART_PARITY = esphome::uart::UART_CONFIG_PARITY_NONE;

// Constants from manual
static constexpr float MAX_THEORETICAL_RANGE = 10.0f;  // Max 10m for movement
static constexpr float MOVEMENT_RANGE = 10.0f;         // Max 10m for movement
static constexpr float MICROMOVEMENT_RANGE = 6.0f;     // Max 6m for micro-movement
static constexpr float STATIC_RANGE = 5.0f;            // Max 5m for static detection
static constexpr float DISTANCE_PRECISION = 0.15f;     // ±0.15m accuracy
static constexpr float DISTANCE_GATE_SIZE = 0.7f;      // 0.7m per gate

// Add calibration coefficients
static const uint8_t DEFAULT_COEFF = 0x1E;  // Default coefficient (3.0)
static const float MIN_COEFF = 1.0f;
static const float MAX_COEFF = 20.0f;

class HLKLD2402Component : public Component, public uart::UARTDevice {
public:
  float get_setup_priority() const override { return setup_priority::LATE; }

  void set_distance_sensor(sensor::Sensor *distance_sensor) { distance_sensor_ = distance_sensor; }
  void set_distance_throttle(uint32_t throttle_ms) { distance_throttle_ms_ = throttle_ms; }
  void set_presence_binary_sensor(binary_sensor::BinarySensor *presence) { presence_binary_sensor_ = presence; }
  void set_micromovement_binary_sensor(binary_sensor::BinarySensor *micro) { micromovement_binary_sensor_ = micro; }
  void set_power_interference_binary_sensor(binary_sensor::BinarySensor *power_interference) { power_interference_binary_sensor_ = power_interference; }
  void set_max_distance(float max_distance) { max_distance_ = max_distance; }
  void set_timeout(uint32_t timeout) { timeout_ = timeout; }
  
  void set_firmware_version_text_sensor(text_sensor::TextSensor *version_sensor) { 
    this->firmware_version_text_sensor_ = version_sensor; 
  }
  
  void set_operating_mode_text_sensor(text_sensor::TextSensor *mode_sensor) {
    this->operating_mode_text_sensor_ = mode_sensor;
  }
  
  void set_calibration_progress_sensor(sensor::Sensor *calibration_progress) { calibration_progress_sensor_ = calibration_progress; }
  
  void setup() override;
  void loop() override;
  void dump_config() override;
  
  void calibrate();
  void save_config();
  void enable_auto_gain();
  void check_power_interference();
  void factory_reset();  // Add new factory reset method
  
  // Add a new public method to set the work mode
  void set_engineering_mode() {
    set_work_mode_(MODE_ENGINEERING);
  }
  
  void set_normal_mode() {
    set_work_mode_(MODE_NORMAL);
  }
  
  void get_serial_number();

protected:
  bool enter_config_mode_();
  bool enter_config_mode_quick_();  // New quick entry method
  bool exit_config_mode_();
  bool send_command_(uint16_t command, const uint8_t *data = nullptr, size_t len = 0);
  bool read_response_(std::vector<uint8_t> &response, uint32_t timeout_ms = 1000);  // Added timeout parameter
  bool set_parameter_(uint16_t param_id, uint32_t value);
  bool get_parameter_(uint16_t param_id, uint32_t &value);
  bool set_work_mode_(uint32_t mode);
  bool set_work_mode_with_timeout_(uint32_t mode, uint32_t timeout_ms);  // New method with timeout
  void process_line_(const std::string &line);
  void dump_hex_(const uint8_t *data, size_t len, const char* prefix);
  bool write_frame_(const std::vector<uint8_t> &frame);  // New method
  void get_firmware_version_();  // Add the missing function declaration
  void begin_passive_version_detection_();  // New method for passive detection
  void publish_operating_mode_();  // New method to publish the current operating mode
  
  bool save_configuration_();
  bool enable_auto_gain_();
  bool get_serial_number_hex_();
  bool get_serial_number_char_();

  // Convert dB value to raw threshold
  uint32_t db_to_threshold_(float db_value);
  float threshold_to_db_(uint32_t threshold);

  bool parse_data_frame_(const std::vector<uint8_t> &frame_data);
  bool process_distance_frame_(const std::vector<uint8_t> &frame_data);
  void update_binary_sensors_(float distance_cm);  // New helper method

private:
  // According to manual, response timeout should be 1s
  static const uint32_t RESPONSE_TIMEOUT_MS = 1000;
  
  sensor::Sensor *distance_sensor_{nullptr};
  sensor::Sensor *calibration_progress_sensor_{nullptr};
  binary_sensor::BinarySensor *presence_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *micromovement_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *power_interference_binary_sensor_{nullptr};
  
  text_sensor::TextSensor *firmware_version_text_sensor_{nullptr};
  text_sensor::TextSensor *operating_mode_text_sensor_{nullptr};
  
  float max_distance_{5.0};
  uint32_t timeout_{5};
  bool config_mode_{false};
  std::string firmware_version_;
  std::string line_buffer_;
  bool power_interference_detected_{false};
  uint32_t last_calibration_status_{0};
  bool calibration_in_progress_{false};
  uint32_t last_calibration_check_{0};   // Time of last calibration check
  uint32_t calibration_progress_{0};     // Current calibration progress (0-100)
  std::string serial_number_; // Add field to store serial number
  std::string operating_mode_{"Normal"};  // Track the current operating mode
  uint32_t last_distance_update_{0};   // Time of last distance sensor update
  uint32_t distance_throttle_ms_{2000}; // Default throttle of 2 seconds
};

}  // namespace hlk_ld2402
}  // namespace esphome