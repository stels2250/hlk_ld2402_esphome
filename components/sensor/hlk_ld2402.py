import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import sensor
from esphome.const import CONF_ID

from .. import hlk_ld2402_ns, HLKLD2402Component

DEPENDENCIES = ['hlk_ld2402']
AUTO_LOAD = ['hlk_ld2402']

CONF_HLK_LD2402_ID = "hlk_ld2402_id"

HLKLD2402Sensor = hlk_ld2402_ns.class_(
    "HLKLD2402Sensor", sensor.Sensor, cg.Component
)

CONFIG_SCHEMA = sensor.SENSOR_SCHEMA.extend({
    cv.GenerateID(): cv.declare_id(HLKLD2402Sensor),
    cv.Required(CONF_HLK_LD2402_ID): cv.use_id(HLKLD2402Component),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await sensor.register_sensor(var, config)
    
    parent = await cg.get_variable(config[CONF_HLK_LD2402_ID])
    cg.add(var.set_parent(parent))