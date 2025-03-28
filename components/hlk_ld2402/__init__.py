import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID

CODEOWNERS = ["@mouldybread"]
DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor", "binary_sensor"]

hlk_ld2402_ns = cg.esphome_ns.namespace("hlk_ld2402")
HLKLD2402Component = hlk_ld2402_ns.class_(
    "HLKLD2402Component", cg.Component, uart.UARTDevice
)

CONF_MAX_DISTANCE = "max_distance"
CONF_DISAPPEAR_DELAY = "disappear_delay"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(HLKLD2402Component),
    cv.Optional(CONF_MAX_DISTANCE, default=8.5): cv.float_range(min=0.7, max=10.0),
    cv.Optional(CONF_DISAPPEAR_DELAY, default=30): cv.uint16_t,
}).extend(cv.COMPONENT_SCHEMA).extend(uart.UART_DEVICE_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    cg.add(var.set_max_distance(config[CONF_MAX_DISTANCE]))
    cg.add(var.set_disappear_delay(config[CONF_DISAPPEAR_DELAY]))