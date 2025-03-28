import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_ID,
    CONF_UNIT_OF_MEASUREMENT,
    CONF_ACCURACY_DECIMALS,
    CONF_ICON,
    CONF_DEVICE_CLASS
)
from esphome import automation
from .. import hlk_ld2402_ns, HLKLD2402Component

DEPENDENCIES = ['hlk_ld2402']
AUTO_LOAD = ['hlk_ld2402']

CONF_HLK_LD2402_ID = "hlk_ld2402_id"

# Sensor class with all required functionality
HLKLD2402Sensor = hlk_ld2402_ns.class_(
    "HLKLD2402Sensor", 
    sensor.Sensor, 
    cg.PollingComponent,
    cg.Parented.template(HLKLD2402Component)
)

# Configuration schema with all options
CONFIG_SCHEMA = cv.All(
    sensor.sensor_schema(
        HLKLD2402Sensor,
        unit_of_measurement="m",
        accuracy_decimals=2,
        icon="mdi:radar",
        device_class="distance"
    ).extend({
        cv.Required(CONF_HLK_LD2402_ID): cv.use_id(HLKLD2402Component),
        cv.Optional("filters"): cv.ensure_list(cv.Schema({
            cv.Required("sliding_window_moving_average"): cv.Schema({
                cv.Optional("window_size", default=5): cv.int_range(min=1, max=20),
                cv.Optional("send_every", default=1): cv.int_range(min=1, max=10),
                cv.Optional("send_first_at", default=1): cv.int_range(min=1, max=10),
            })
        }))
    }),
    cv.has_at_least_one_key(CONF_UNIT_OF_MEASUREMENT, CONF_ICON, CONF_DEVICE_CLASS)
).extend(cv.polling_component_schema('60s'))

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await sensor.register_sensor(var, config)
    
    # Add parent reference
    parent = await cg.get_variable(config[CONF_HLK_LD2402_ID])
    cg.add(var.set_parent(parent))
    
    # Add filters if specified
    if "filters" in config:
        for filt in config["filters"]:
            if "sliding_window_moving_average" in filt:
                conf = filt["sliding_window_moving_average"]
                cg.add(var.add_filter_sliding_window_moving_average(
                    conf["window_size"],
                    conf["send_every"],
                    conf["send_first_at"]
                ))