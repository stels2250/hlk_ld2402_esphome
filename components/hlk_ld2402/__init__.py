import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, sensor, binary_sensor
from esphome.const import CONF_ID

DEPENDENCIES = ['uart']
AUTO_LOAD = ['binary_sensor', 'sensor']
CODEOWNERS = ['@mouldybread']

hlk_ld2402_ns = cg.esphome_ns.namespace('hlk_ld2402')
HLKLD2402 = hlk_ld2402_ns.class_('HLKLD2402', cg.Component, uart.UARTDevice)

# Configuration for the main component
CONF_HLK_LD2402 = 'hlk_ld2402'
CONF_MAX_DISTANCE = 'max_distance'
CONF_DISAPPEAR_DELAY = 'disappear_delay'

HLK_LD2402_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(HLKLD2402),
    cv.Optional(CONF_MAX_DISTANCE, default=8.5): cv.float_range(min=0.7, max=10.0),
    cv.Optional(CONF_DISAPPEAR_DELAY, default=30): cv.uint16_t,
}).extend(cv.COMPONENT_SCHEMA).extend(uart.UART_DEVICE_SCHEMA)

# Configuration for sensors
CONF_DISTANCE = 'distance'
CONF_PRESENCE = 'presence'
CONF_MOVEMENT = 'movement'
CONF_MICROMOVEMENT = 'micromovement'

CONFIG_SCHEMA = cv.Schema({
    cv.Optional(CONF_HLK_LD2402): HLK_LD2402_SCHEMA,
    cv.Optional(CONF_DISTANCE): sensor.sensor_schema(
        unit_of_measurement="m",
        accuracy_decimals=2,
    ),
    cv.Optional(CONF_PRESENCE): binary_sensor.binary_sensor_schema(
        device_class="presence"
    ),
    cv.Optional(CONF_MOVEMENT): binary_sensor.binary_sensor_schema(),
    cv.Optional(CONF_MICROMOVEMENT): binary_sensor.binary_sensor_schema(),
})

async def to_code(config):
    # Main component
    if CONF_HLK_LD2402 in config:
        var = cg.new_Pvariable(config[CONF_HLK_LD2402][CONF_ID])
        await cg.register_component(var, config[CONF_HLK_LD2402])
        await uart.register_uart_device(var, config[CONF_HLK_LD2402])
        
        cg.add(var.set_max_distance(config[CONF_HLK_LD2402][CONF_MAX_DISTANCE]))
        cg.add(var.set_disappear_delay(config[CONF_HLK_LD2402][CONF_DISAPPEAR_DELAY]))
    
    # Sensors
    if CONF_DISTANCE in config:
        sens = await sensor.new_sensor(config[CONF_DISTANCE])
        cg.add(var.set_distance_sensor(sens))
    if CONF_PRESENCE in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_PRESENCE])
        cg.add(var.set_presence_sensor(sens))
    if CONF_MOVEMENT in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_MOVEMENT])
        cg.add(var.set_movement_sensor(sens))
    if CONF_MICROMOVEMENT in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_MICROMOVEMENT])
        cg.add(var.set_micromovement_sensor(sens))