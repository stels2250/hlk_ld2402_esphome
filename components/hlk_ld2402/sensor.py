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

# Create standard distance sensor schema
DISTANCE_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_CENTIMETER,
    accuracy_decimals=1,
    device_class=DEVICE_CLASS_DISTANCE,
    state_class=STATE_CLASS_MEASUREMENT,
).extend({
    cv.GenerateID(): cv.declare_id(sensor.Sensor),
    cv.Required(CONF_HLK_LD2402_ID): cv.use_id(HLKLD2402Component),
    cv.Optional(CONF_THROTTLE): cv.positive_time_period_milliseconds,
})

# Create calibration progress sensor schema
CALIBRATION_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_PERCENT,
    accuracy_decimals=0,
    state_class=STATE_CLASS_DIAGNOSTIC,
    icon="mdi:progress-wrench"
).extend({
    cv.GenerateID(): cv.declare_id(sensor.Sensor),
    cv.Required(CONF_HLK_LD2402_ID): cv.use_id(HLKLD2402Component),
    cv.Optional(CONF_CALIBRATION_PROGRESS, default=True): cv.boolean,
})

CONFIG_SCHEMA = cv.typed_schema({
    CONF_CALIBRATION_PROGRESS: CALIBRATION_SCHEMA,
    cv.CONF_EMPTY: DISTANCE_SCHEMA,  # Default to distance sensor
})

async def to_code(config):
    parent = await cg.get_variable(config[CONF_HLK_LD2402_ID])
    var = await sensor.new_sensor(config)
    
    if CONF_CALIBRATION_PROGRESS in config:
        cg.add(parent.set_calibration_progress_sensor(var))
    else:  # Regular distance sensor
        cg.add(parent.set_distance_sensor(var))
        if CONF_THROTTLE in config:
            cg.add(parent.set_distance_throttle(config[CONF_THROTTLE]))
