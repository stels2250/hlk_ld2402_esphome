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
# Engineering Mode and Energy Gates

## Understanding Radar Gates

The HLK-LD2402 divides its detection range into multiple "gates" (distance segments):

- Each gate is approximately 0.7m in length
- The full 10m range is covered by 14 gates (0-13)
- Energy levels at each gate are used to determine presence detection

## Energy Gate Sensors

In engineering mode, you can access the raw energy levels from each gate. These are valuable for:

- Tuning detection sensitivity 
- Diagnosing interference issues
- Visualizing radar performance

## Adding Energy Gate Sensors

Add energy gate sensors to your configuration:

```yaml
sensor:
  - platform: hlk_ld2402
    name: "Radar Energy Gate 0"
    hlk_ld2402_id: radar_sensor
    state_class: measurement
    unit_of_measurement: "dB"
    icon: "mdi:antenna"
    entity_category: diagnostic
    energy_gate:
      gate_index: 0  # First gate (0-0.7m)
```

## Using Engineering Mode

1. Configure the energy gate sensors you want to monitor
2. Press the "Engineering Mode Toggle" button to switch to engineering mode
3. Energy values will be reported in dB for each configured gate
4. Use the values to debug sensitivity issues or optimize placement

## Interpreting Energy Values

- Higher dB values indicate stronger reflections
- Values typically range from 0-60 dB depending on the environment
- Human presence usually causes a noticeable elevation in energy levels
- Each gate has separate thresholds for motion and micromovement detection

## Adjusting Detection Thresholds

The HLK-LD2402 uses threshold values per distance gate to determine motion and micromovement detection. 
You can adjust these thresholds for fine-tuning the sensor's sensitivity.

### Threshold Types

1. **Motion Thresholds**: Control when movement is detected (gates 0-15)
2. **Micromotion Thresholds**: Control when micromovement (slight motion) is detected (gates 0-15)

### Setting Thresholds via Services

The component exposes services to adjust thresholds:

```yaml
# Set motion threshold for a specific gate
- service: esphome.radar_sensor_set_motion_threshold
  data:
    gate: 0  # Gate index (0-15)
    db_value: 45.0  # Threshold in dB (0-95)

# Set micromotion threshold for a specific gate
- service: esphome.radar_sensor_set_micromotion_threshold
  data:
    gate: 0  # Gate index (0-15)
    db_value: 40.0  # Threshold in dB (0-95)
```

### Automatic Threshold Generation

You can also use calibration with custom coefficients:

```yaml
# Calibrate with custom sensitivity coefficients
- service: esphome.radar_sensor_calibrate_with_coefficients
  data:
    trigger_coefficient: 3.5  # Motion trigger coefficient (1.0-20.0)
    hold_coefficient: 3.0  # Hold/presence coefficient (1.0-20.0)  
    micromotion_coefficient: 4.0  # Micromotion coefficient (1.0-20.0)
```

Higher coefficient values result in higher thresholds (less sensitive).
Lower coefficient values result in lower thresholds (more sensitive).

### Understanding Threshold Values

The raw energy values shown in engineering mode can help you determine appropriate threshold values:
1. Switch to engineering mode
2. Move around the space at different distances
3. Note the energy values for each gate
4. Set thresholds slightly below those energy values

When the energy level exceeds a threshold, the corresponding detection (motion or micromovement) is triggered.

## Gate Distance Reference

| Gate Index | Distance Range |
|------------|---------------|
| 0 | 0.0m - 0.7m |
| 1 | 0.7m - 1.4m |
| 2 | 1.4m - 2.1m |
| 3 | 2.1m - 2.8m |
| 4 | 2.8m - 3.5m |
| 5 | 3.5m - 4.2m |
| 6 | 4.2m - 4.9m |
| 7 | 4.9m - 5.6m |
| 8 | 5.6m - 6.3m |
| 9 | 6.3m - 7.0m |
| 10 | 7.0m - 7.7m |
| 11 | 7.7m - 8.4m |
| 12 | 8.4m - 9.1m |
| 13 | 9.1m - 9.8m |

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
