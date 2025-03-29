import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_DISTANCE,
    STATE_CLASS_MEASUREMENT,
    UNIT_CENTIMETER,
    STATE_CLASS_DIAGNOSTIC,
    UNIT_PERCENT,
)

from . import HLKLD2402Component, CONF_HLK_LD2402_ID

CONF_THROTTLE = "throttle"
CONF_CALIBRATION_PROGRESS = "calibration_progress"

# Define a single schema with optional fields for both sensor types
CONFIG_SCHEMA = sensor.sensor_schema().extend({
    cv.GenerateID(): cv.declare_id(sensor.Sensor),
    cv.Required(CONF_HLK_LD2402_ID): cv.use_id(HLKLD2402Component),
    cv.Optional(CONF_THROTTLE): cv.positive_time_period_milliseconds,
    cv.Optional(CONF_CALIBRATION_PROGRESS, default=False): cv.boolean,
})

async def to_code(config):
    parent = await cg.get_variable(config[CONF_HLK_LD2402_ID])
    var = await sensor.new_sensor(config)
    
    if config.get(CONF_CALIBRATION_PROGRESS):
        # This is a calibration progress sensor
        cg.add(parent.set_calibration_progress_sensor(var))
    else:
        # This is a regular distance sensor
        cg.add(parent.set_distance_sensor(var))
        if CONF_THROTTLE in config:
            cg.add(parent.set_distance_throttle(config[CONF_THROTTLE]))
