import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, sensor
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_DISTANCE,
    STATE_CLASS_MEASUREMENT,
    UNIT_METER,
    UNIT_CENTIMETER,
)

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor"]

hlk_ld2402_ns = cg.esphome_ns.namespace("hlk_ld2402")
HLKLD2402Component = hlk_ld2402_ns.class_(
    "HLKLD2402Component", cg.Component, uart.UARTDevice
)

CONF_DISTANCE = "distance"
CONF_DISTANCE_IN_CM = "distance_in_cm"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(HLKLD2402Component),
    cv.Optional(CONF_DISTANCE): sensor.sensor_schema(
        device_class=DEVICE_CLASS_DISTANCE,
        state_class=STATE_CLASS_MEASUREMENT,
    ).extend({
        cv.Optional(CONF_DISTANCE_IN_CM, default=True): cv.boolean,
    }),
}).extend(cv.COMPONENT_SCHEMA).extend(uart.UART_DEVICE_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    if CONF_DISTANCE in config:
        conf = config[CONF_DISTANCE]
        sens = await sensor.new_sensor(conf)
        cg.add(var.set_distance_sensor(sens))
        
        if CONF_DISTANCE_IN_CM in conf:
            in_cm = conf[CONF_DISTANCE_IN_CM]
            if in_cm:
                sens.unit_of_measurement = UNIT_CENTIMETER
                sens.accuracy_decimals = 0
            else:
                sens.unit_of_measurement = UNIT_METER
                sens.accuracy_decimals = 2
            cg.add(var.set_distance_in_cm(in_cm))