import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from esphome.const import CONF_ID, CONF_NAME

from . import HLKLD2402Component, CONF_HLK_LD2402_ID

# Button action types
CONF_CALIBRATE = "calibrate"
CONF_AUTO_GAIN = "auto_gain"
CONF_SAVE_CONFIG = "save_config"
CONF_ENGINEERING_MODE = "engineering_mode"

# Define a schema for each button type
BUTTON_SCHEMA = cv.Schema({
    cv.Required(CONF_HLK_LD2402_ID): cv.use_id(HLKLD2402Component),
})

# Define the validation schema for the buttons
CONFIG_SCHEMA = cv.Schema({
    cv.Optional(CONF_CALIBRATE): button.button_schema().extend(BUTTON_SCHEMA),
    cv.Optional(CONF_AUTO_GAIN): button.button_schema().extend(BUTTON_SCHEMA),
    cv.Optional(CONF_SAVE_CONFIG): button.button_schema().extend(BUTTON_SCHEMA),
    cv.Optional(CONF_ENGINEERING_MODE): button.button_schema().extend(BUTTON_SCHEMA),
})

async def to_code(config):
    for key, conf in config.items():
        if key not in [CONF_CALIBRATE, CONF_AUTO_GAIN, CONF_SAVE_CONFIG, CONF_ENGINEERING_MODE]:
            continue
        
        var = await button.new_button(conf)
        parent = await cg.get_variable(conf[CONF_HLK_LD2402_ID])
        
        if key == CONF_CALIBRATE:
            cg.add(var.set_on_press([parent.calibrate()]))
        elif key == CONF_AUTO_GAIN:
            cg.add(var.set_on_press([parent.enable_auto_gain()]))
        elif key == CONF_SAVE_CONFIG:
            cg.add(var.set_on_press([parent.save_config()]))
        elif key == CONF_ENGINEERING_MODE:
            # Per the documentation, engineering mode is 0x04
            cg.add(var.set_on_press([parent.set_work_mode_(4)]))
