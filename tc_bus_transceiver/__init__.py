import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome import automation
from esphome import pins
from esphome.const import (
    CONF_TRIGGER_ID,
)

tc_bus_transceiver_ns = cg.esphome_ns.namespace('tc_bus_transceiver')
TcBusTransceiver = tc_bus_transceiver_ns.class_('TcBusTransceiver', cg.Component)

# Triggers
MessageTrigger = tc_bus_transceiver_ns.class_("MessageTrigger", automation.Trigger.template(cg.uint32))

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(TcBusTransceiver),
    cv.Required("input_pin"): pins.gpio_input_pin_schema,
    cv.Required("output_pin"): pins.gpio_output_pin_schema,
    cv.Optional("on_message"): automation.validate_automation(
        {
            cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(MessageTrigger),
        }
    ),
}).extend(cv.COMPONENT_SCHEMA)

def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    yield cg.register_component(var, config)
    
    pin_input = yield cg.gpio_pin_expression(config["input_pin"])
    cg.add(var.set_input_pin(pin_input))
    pin_output = yield cg.gpio_pin_expression(config["output_pin"])
    cg.add(var.set_output_pin(pin_output))
    
    for conf in config.get("on_message", []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.register_trigger(trigger))
        yield automation.build_automation(trigger, [(cg.uint32, "message_data")], conf)