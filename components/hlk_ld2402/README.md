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
