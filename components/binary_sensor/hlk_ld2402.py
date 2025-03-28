import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import CONF_ID

from .. import hlk_ld2402_ns, HLKLD2402Component

DEPENDENCIES = ['hlk_ld2402']
AUTO_LOAD = ['hlk_ld2402']

CONF_HLK_LD2402_ID = "hlk_ld2402_id"

HLKLD2402Sensor = hlk_ld2402_ns.class_(
    "HLKLD2402Sensor", sensor.Sensor, cg.Component
)

def validate_config(config):
    # Ensure required fields are present
    if CONF_HLK_LD2402_ID not in config:
        raise cv.Invalid("hlk_ld2402_id is required")
    return config

CONFIG_SCHEMA = cv.All(
    sensor.sensor_schema(
        HLKLD2402Sensor,
        unit_of_measurement="m",
        accuracy_decimals=2,
    ).extend({
        cv.Required(CONF_HLK_LD2402_ID): cv.use_id(HLKLD2402Component),
    }),
    validate_config
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await sensor.register_sensor(var, config)
    
    parent = await cg.get_variable(config[CONF_HLK_LD2402_ID])
    cg.add(var.set_parent(parent))