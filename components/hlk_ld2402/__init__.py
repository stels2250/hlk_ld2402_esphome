"""
HLK-LD2402 Millimeter Wave Radar Presence Sensor component for ESPHome.

This component implements support for the HLK-LD2402 presence detection sensor.
"""

from esphome import pins
from esphome.components import uart
from esphome.const import (
    CONF_ID,
    CONF_UART_ID,
)

CODEOWNERS = ["@mouldybread"]
DEPENDENCIES = ["uart"]
AUTO_LOAD = ["binary_sensor", "sensor"]

hlk_ld2402_ns = cg.esphome_ns.namespace("hlk_ld2402")
HLKLD2402Component = hlk_ld2402_ns.class_(
    "HLK_LD2402", cg.Component, uart.UARTDevice
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(HLKLD2402Component),
            cv.Optional("max_distance", default=8.5): cv.float_range(
                min=0.7, max=10.0
            ),
            cv.Optional("disappear_delay", default=30): cv.uint16_t,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    cg.add(var.set_max_distance(config["max_distance"]))
    cg.add(var.set_disappear_delay(config["disappear_delay"]))