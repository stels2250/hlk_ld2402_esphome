import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_MOTION,
    DEVICE_CLASS_OCCUPANCY,
)
from .. import hlk_ld2402_ns, HLKLD2402Component

DEPENDENCIES = ['hlk_ld2402']
AUTO_LOAD = ['hlk_ld2402']

CONF_HLK_LD2402_ID = "hlk_ld2402_id"
CONF_TYPE = "type"

TYPES = {
    "presence": DEVICE_CLASS_OCCUPANCY,
    "movement": DEVICE_CLASS_MOTION,
    "micromovement": DEVICE_CLASS_MOTION,
}

HLKLD2402BinarySensor = hlk_ld2402_ns.class_(
    "HLKLD2402BinarySensor", 
    binary_sensor.BinarySensor, 
    cg.Component
)

CONFIG_SCHEMA = binary_sensor.binary_sensor_schema(
    HLKLD2402BinarySensor
).extend({
    cv.Required(CONF_HLK_LD2402_ID): cv.use_id(HLKLD2402Component),
    cv.Required(CONF_TYPE): cv.enum(TYPES, lower=True),
})

async def to_code(config):
    var = await binary_sensor.new_binary_sensor(config)
    await cg.register_component(var, config)
    
    parent = await cg.get_variable(config[CONF_HLK_LD2402_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_type(config[CONF_TYPE]))