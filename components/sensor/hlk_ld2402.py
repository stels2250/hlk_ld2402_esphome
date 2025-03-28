import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_DISTANCE,
    STATE_CLASS_MEASUREMENT,
    UNIT_METER,
)
from .. import hlk_ld2402_ns, HLKLD2402Component

DEPENDENCIES = ['hlk_ld2402']
AUTO_LOAD = ['hlk_ld2402']

CONF_HLK_LD2402_ID = "hlk_ld2402_id"
CONF_DISTANCE = "distance"

HLKLD2402DistanceSensor = hlk_ld2402_ns.class_(
    "HLKLD2402DistanceSensor",
    sensor.Sensor,
    cg.Component
)

DISTANCE_SCHEMA = sensor.sensor_schema(
    HLKLD2402DistanceSensor,
    unit_of_measurement=UNIT_METER,
    device_class=DEVICE_CLASS_DISTANCE,
    state_class=STATE_CLASS_MEASUREMENT,
    icon="mdi:radar",
    accuracy_decimals=2,
).extend({
    cv.Required(CONF_HLK_LD2402_ID): cv.use_id(HLKLD2402Component),
})

CONFIG_SCHEMA = cv.Schema({
    cv.Required(CONF_DISTANCE): DISTANCE_SCHEMA,
})

async def to_code(config):
    distance_conf = config[CONF_DISTANCE]
    var = cg.new_Pvariable(distance_conf[CONF_ID])
    await cg.register_component(var, distance_conf)
    await sensor.register_sensor(var, distance_conf)
    
    parent = await cg.get_variable(distance_conf[CONF_HLK_LD2402_ID])
    cg.add(var.set_parent(parent))