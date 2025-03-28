import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import (
    CONF_ID,
    CONF_DEVICE_CLASS,
    DEVICE_CLASS_PRESENCE,
    DEVICE_CLASS_MOTION,
)

from . import HLKLD2402Component, CONF_HLK_LD2402_ID

# Define the schema for binary sensors
CONFIG_SCHEMA = binary_sensor.binary_sensor_schema().extend({
    cv.GenerateID(): cv.declare_id(binary_sensor.BinarySensor),
    cv.Required(CONF_HLK_LD2402_ID): cv.use_id(HLKLD2402Component),
    cv.Required(CONF_DEVICE_CLASS): cv.one_of(DEVICE_CLASS_PRESENCE, DEVICE_CLASS_MOTION),
})

async def to_code(config):
    parent = await cg.get_variable(config[CONF_HLK_LD2402_ID])
    var = await binary_sensor.new_binary_sensor(config)
    
    if config[CONF_DEVICE_CLASS] == DEVICE_CLASS_PRESENCE:
        cg.add(parent.set_presence_binary_sensor(var))
    elif config[CONF_DEVICE_CLASS] == DEVICE_CLASS_MOTION:
        cg.add(parent.set_micromovement_binary_sensor(var))
