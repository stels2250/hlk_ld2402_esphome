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
from .. import hlk_ld2402_ns, HLKLD2402Component

DEPENDENCIES = ['hlk_ld2402']
AUTO_LOAD = ['hlk_ld2402']

CONF_HLK_LD2402_ID = "hlk_ld2402_id"
CONF_DISTANCE = "distance"

HLKLD2402DistanceSensor = hlk_ld2402_ns.class_(
    "HLKLD2402DistanceSensor",
    sensor.Sensor,
    cg.Component,
    cg.Parented.template(HLKLD2402Component)
)

def validate_distance_sensor(config):
    # Ensure required fields are present
    if CONF_UNIT_OF_MEASUREMENT not in config:
        config[CONF_UNIT_OF_MEASUREMENT] = "m"
    if CONF_ACCURACY_DECIMALS not in config:
        config[CONF_ACCURACY_DECIMALS] = 2
    return config

DISTANCE_SCHEMA = cv.All(
    sensor.sensor_schema(
        HLKLD2402DistanceSensor,
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
    validate_distance_sensor
)

CONFIG_SCHEMA = cv.Schema({
    cv.Required(CONF_DISTANCE): DISTANCE_SCHEMA,
})

async def to_code(config):
    conf = config[CONF_DISTANCE]
    var = cg.new_Pvariable(conf[CONF_ID])
    await cg.register_component(var, conf)
    await sensor.register_sensor(var, conf)
    
    parent = await cg.get_variable(conf[CONF_HLK_LD2402_ID])
    cg.add(var.set_parent(parent))
    
    if "filters" in conf:
        for filt in conf["filters"]:
            if "sliding_window_moving_average" in filt:
                fconf = filt["sliding_window_moving_average"]
                cg.add(var.add_filter_sliding_window_moving_average(
                    fconf["window_size"],
                    fconf["send_every"],
                    fconf["send_first_at"]
                ))