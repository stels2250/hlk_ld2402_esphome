import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, binary_sensor, sensor
from esphome.const import CONF_ID

# Required ESPHome declarations
CODEOWNERS = ["@mouldybread"]
DEPENDENCIES = ["uart"]
AUTO_LOAD = ["binary_sensor", "sensor"]

# Configuration variables
CONF_HLK_LD2402 = "hlk_ld2402"
CONF_PRESENCE = "presence"
CONF_DISTANCE = "distance"
CONF_MOVEMENT = "movement"
CONF_MICROMOVEMENT = "micromovement"
CONF_MAX_DISTANCE = "max_distance"
CONF_DISAPPEAR_DELAY = "disappear_delay"

# Namespace setup
hlk_ld2402_ns = cg.esphome_ns.namespace("hlk_ld2402")
HLKLD2402Component = hlk_ld2402_ns.class_(
    "HLKLD2402Component", cg.Component, uart.UARTDevice
)

# Sensor schemas
PRESENCE_SCHEMA = binary_sensor.binary_sensor_schema(device_class="presence")
MOVEMENT_SCHEMA = binary_sensor.binary_sensor_schema()
MICROMOVEMENT_SCHEMA = binary_sensor.binary_sensor_schema()
DISTANCE_SCHEMA = sensor.sensor_schema(
    unit_of_measurement="m",
    accuracy_decimals=2,
)

# Main component schema
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(HLKLD2402Component),
    cv.Optional(CONF_MAX_DISTANCE, default=8.5): cv.float_range(min=0.7, max=10.0),
    cv.Optional(CONF_DISAPPEAR_DELAY, default=30): cv.uint16_t,
    cv.Optional(CONF_PRESENCE): PRESENCE_SCHEMA,
    cv.Optional(CONF_DISTANCE): DISTANCE_SCHEMA,
    cv.Optional(CONF_MOVEMENT): MOVEMENT_SCHEMA,
    cv.Optional(CONF_MICROMOVEMENT): MICROMOVEMENT_SCHEMA,
}).extend(cv.COMPONENT_SCHEMA).extend(uart.UART_DEVICE_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    
    cg.add(var.set_max_distance(config[CONF_MAX_DISTANCE]))
    cg.add(var.set_disappear_delay(config[CONF_DISAPPEAR_DELAY]))
    
    if CONF_PRESENCE in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_PRESENCE])
        cg.add(var.set_presence_sensor(sens))
    if CONF_DISTANCE in config:
        sens = await sensor.new_sensor(config[CONF_DISTANCE])
        cg.add(var.set_distance_sensor(sens))
    if CONF_MOVEMENT in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_MOVEMENT])
        cg.add(var.set_movement_sensor(sens))
    if CONF_MICROMOVEMENT in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_MICROMOVEMENT])
        cg.add(var.set_micromovement_sensor(sens))