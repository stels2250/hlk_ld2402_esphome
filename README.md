# HLK-LD2402 ESPHome Component

This component integrates the HLK-LD2402 24GHz radar module with ESPHome, providing presence detection, micromovement sensing, and distance measurement.

## Module Overview

The HLK-LD2402 is a 24GHz millimeter-wave radar sensor from Hi-Link that can detect human presence, motion, and micromotion. It's particularly useful for IoT applications where accurate presence detection is required even when subjects are stationary.

### Key Features from Technical Documentation

- **Detection Capabilities**:
  - Movement detection up to 10m
  - Micromovement detection up to 6m
  - Static human presence detection up to 5m
  - Horizontal detection angle of ±60°

- **Technical Specifications**:
  - 24GHz ISM frequency band (24.0-24.25GHz)
  - Distance measurement accuracy: ±0.15m
  - Power supply: 3.0-3.6V (standard 3.3V) or 4.5-5.5V with LDO
  - Average current consumption: 50mA
  - Communication: UART (115200 baud rate, 8N1)
  - Operating temperature: -40°C to 85°C
  - Compact size: 20mm × 20mm

## ESPHome Integration Features

- **Sensor Integration**:
  - Distance sensor (cm)
  - Presence binary sensor
  - Micromovement binary sensor

- **Control Features**:
  - Calibration button to optimize detection thresholds
  - Auto-gain function to adjust sensitivity
  - Configuration saving capability
  - Engineering mode for advanced testing

## Installation

1. Add this repository to your ESPHome external components:
   ```yaml
   external_components:
     - source:
         type: git
         url: https://github.com/mouldybread/hlk_ld2402_esphome
       refresh: 0ms
   ```

2. Configure the component with the example YAML below

3. Flash your ESP device with the configuration

## Common Pitfalls and Solutions

- **Power Interference**: The radar module performs a power supply check during startup. Ensure you use a clean, stable power source.

- **Detection Issues**: 
  - For ceiling mount: Maximum effective detection range is reduced compared to wall mount
  - Ensure no metal objects are placed between the sensor and detection area
  - The module cannot detect through metal surfaces

- **Calibration Requirements**:
  - Perform calibration in an empty room with no movement
  - Allow 10-15 seconds for the calibration process to complete

- **Installation Considerations**:
  - Wall mounting: Install at 1.5-2.0m height for optimal results
  - Ceiling mounting: Install at 2.7-3.0m height
  - Avoid installing near moving objects like fans or curtains

- **Sensor Orientation**:
  - For static detection, the sensor should face the subject directly
  - The Y-axis of the sensor should point toward the detection area

## Example YAML Configuration

```yaml
# UART configuration for HLK-LD2402
uart:
  id: uart_bus
  tx_pin: GPIO1  # TX0 hardware UART
  rx_pin: GPIO3  # RX0 hardware UART
  baud_rate: 115200
  data_bits: 8
  parity: NONE
  stop_bits: 1

# HLK-LD2402 radar component
external_components:
  - source:
      type: git
      url: https://github.com/mouldybread/hlk_ld2402_esphome
    refresh: 0ms

hlk_ld2402:
  uart_id: uart_bus
  id: radar_sensor
  max_distance: 5.0
  timeout: 5

# Binary sensors
binary_sensor:
  - platform: hlk_ld2402
    id: radar_presence
    name: "Presence"
    device_class: presence
    hlk_ld2402_id: radar_sensor

  - platform: hlk_ld2402
    id: radar_micromovement
    name: "Micromovement"
    device_class: motion
    hlk_ld2402_id: radar_sensor

# Distance sensor
sensor:
  - platform: hlk_ld2402
    id: radar_distance
    name: "Distance"
    hlk_ld2402_id: radar_sensor
    device_class: distance
    unit_of_measurement: "cm"
    accuracy_decimals: 1
    filters:
      - throttle: 2s

# Control buttons - using template buttons 
button:
  - platform: template
    name: "Calibrate"
    on_press:
      - lambda: id(radar_sensor).calibrate();

  - platform: template
    name: "Auto Gain"
    on_press:
      - lambda: id(radar_sensor).enable_auto_gain();

  - platform: template
    name: "Save Config"
    on_press:
      - lambda: id(radar_sensor).save_config();

  - platform: template
    name: "Engineering Mode"
    on_press:
      - lambda: id(radar_sensor).set_engineering_mode();

# Optional - Add status LED if available
status_led:
  pin: GPIO2
```

## Detection Range Visualization

The HLK-LD2402 has different detection capabilities depending on installation:

**Wall Mount Configuration**:
- Movement detection: up to 10m
- Micromovement detection: up to 6m
- Static presence: up to 5m
- Detection angle: ±60°

**Ceiling Mount Configuration**:
- Movement detection: up to 5m radius
- Micromovement detection: up to 4m radius
- Static presence: up to 4m radius
- Static lying person: up to 3m radius

## License

This ESPHome component is released under the MIT License.

## Credits

- Based on Hi-Link HLK-LD2402 technical documentation
- Developed for integration with ESPHome and Home Assistant
