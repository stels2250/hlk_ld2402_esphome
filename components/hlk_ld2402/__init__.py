import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, sensor, binary_sensor
from esphome.const import (
    CONF_ID,
    CONF_NAME,  # Add this
    CONF_DISTANCE,
    DEVICE_CLASS_DISTANCE,
    DEVICE_CLASS_PRESENCE,
    DEVICE_CLASS_MOTION,
    STATE_CLASS_MEASUREMENT,
    UNIT_CENTIMETER,
)

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor", "binary_sensor"]

hlk_ld2402_ns = cg.esphome_ns.namespace("hlk_ld2402")
HLKLD2402Component = hlk_ld2402_ns.class_(
    "HLKLD2402Component", cg.Component, uart.UARTDevice
)

# Register the binary_sensor and sensor platform schemas
sensor.SENSOR_SCHEMA = cv.Schema({}).extend(cv.COMPONENT_SCHEMA)
binary_sensor.BINARY_SENSOR_SCHEMA = cv.Schema({}).extend(cv.COMPONENT_SCHEMA)

# Custom configs
CONF_PRESENCE = "presence"
CONF_MICROMOVEMENT = "micromovement"
CONF_MAX_DISTANCE = "max_distance"
CONF_TIMEOUT = "timeout"
CONF_UART_ID = "uart_id"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(HLKLD2402Component),
    cv.GenerateID(CONF_UART_ID): cv.use_id(uart.UARTComponent),
    cv.Optional(CONF_DISTANCE): sensor.sensor_schema(
        device_class=DEVICE_CLASS_DISTANCE,
        state_class=STATE_CLASS_MEASUREMENT,
        unit_of_measurement=UNIT_CENTIMETER,
        accuracy_decimals=0,
    ),
    cv.Optional(CONF_PRESENCE): binary_sensor.binary_sensor_schema(
        device_class=DEVICE_CLASS_PRESENCE,
    ),
    cv.Optional(CONF_MICROMOVEMENT): binary_sensor.binary_sensor_schema(
        device_class=DEVICE_CLASS_MOTION,
    ),
    cv.Optional(CONF_MAX_DISTANCE, default=10.0): cv.float_range(min=0.7, max=10.0),
    cv.Optional(CONF_TIMEOUT, default=5): cv.int_range(min=0, max=65535),
}).extend(cv.COMPONENT_SCHEMA).extend(uart.UART_DEVICE_SCHEMA)

# These register the platforms
SENSOR_PLATFORM_SCHEMA = sensor.SENSOR_SCHEMA.extend({
    cv.GenerateID(): cv.declare_id(HLKLD2402Component),
})

BINARY_SENSOR_PLATFORM_SCHEMA = binary_sensor.BINARY_SENSOR_SCHEMA.extend({
    cv.GenerateID(): cv.declare_id(HLKLD2402Component),
})

# Add these platform registration functions
@SENSOR_PLATFORM_SCHEMA
async def sensor_to_code(config):
    paren = await cg.get_variable(config[CONF_ID])
    var = await sensor.new_sensor(config)
    cg.add(paren.set_distance_sensor(var))

@BINARY_SENSOR_PLATFORM_SCHEMA
async def binary_sensor_to_code(config):
    paren = await cg.get_variable(config[CONF_ID])
    var = await binary_sensor.new_binary_sensor(config)
    if CONF_PRESENCE in config:
        cg.add(paren.set_presence_binary_sensor(var))
    elif CONF_MICROMOVEMENT in config:
        cg.add(paren.set_micromovement_binary_sensor(var))

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    
    parent = await cg.get_variable(config[CONF_UART_ID])
    cg.add(parent.set_baud_rate(115200))

    if CONF_DISTANCE in config:
        sens = await sensor.new_sensor(config[CONF_DISTANCE])
        cg.add(var.set_distance_sensor(sens))

    if CONF_PRESENCE in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_PRESENCE])
        cg.add(var.set_presence_binary_sensor(sens))

    if CONF_MICROMOVEMENT in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_MICROMOVEMENT])
        cg.add(var.set_micromovement_binary_sensor(sens))

    if CONF_MAX_DISTANCE in config:
        cg.add(var.set_max_distance(config[CONF_MAX_DISTANCE]))

    if CONF_TIMEOUT in config:
        cg.add(var.set_timeout(config[CONF_TIMEOUT]))