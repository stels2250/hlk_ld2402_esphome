import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_DISTANCE,
    STATE_CLASS_MEASUREMENT,
    UNIT_METER,
)
from . import hlk_ld2402_ns, HLKLD2402Component, CONF_HLK_LD2402_ID

HLKLD2402DistanceSensor = hlk_ld2402_ns.class_(
    "HLKLD2402DistanceSensor",
    sensor.Sensor,
    cg.Component
)

CONFIG_SCHEMA = sensor.sensor_schema(
    HLKLD2402DistanceSensor,
    unit_of_measurement=UNIT_METER,
    device_class=DEVICE_CLASS_DISTANCE,
    state_class=STATE_CLASS_MEASUREMENT,
    icon="mdi:radar",
    accuracy_decimals=2,
).extend({
    cv.Required(CONF_HLK_LD2402_ID): cv.use_id(HLKLD2402Component),
})

async def to_code(config):
    var = await sensor.new_sensor(config)
    await cg.register_component(var, config)
    
    parent = await cg.get_variable(config[CONF_HLK_LD2402_ID])
    cg.add(var.set_parent(parent))