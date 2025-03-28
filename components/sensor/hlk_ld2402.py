import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import CONF_ID, CONF_HLK_LD2402_ID

from .. import hlk_ld2402_ns, HLKLD2402Component

DEPENDENCIES = ["hlk_ld2402"]
AUTO_LOAD = ["hlk_ld2402"]

CONF_DISTANCE = "distance"

HLKLD2402Sensor = hlk_ld2402_ns.class_(
    "HLKLD2402Sensor", sensor.Sensor, cg.Component
)

CONFIG_SCHEMA = sensor.sensor_schema(
    HLKLD2402Sensor,
    unit_of_measurement="m",
    accuracy_decimals=2,
).extend({
    cv.GenerateID(CONF_HLK_LD2402_ID): cv.use_id(HLKLD2402Component),
})

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await sensor.register_sensor(var, config)
    
    parent = await cg.get_variable(config[CONF_HLK_LD2402_ID])
    cg.add(var.set_parent(parent))