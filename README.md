# ESPHome External Components

## Usage
[See here for how to use external components](https://esphome.io/components/external_components.html).


## Koch TC:Bus / TCS intercom Interface
Easily receive doorbells and send door opening commands.

### Hardware
The code has been written on a Wemos D1 mini (ESP8260).

Straightforward IO-schematics:
![IO-Schematics](tc_bus_transceiver_schematics.jpg?raw=true)

Beware:
- The output circuit only works due to the very short (some milliseconds) activation periods. Both the resistor and transistor will be damaged if turned on _permanently_ for longer than ~15 milliseconds!
- The input circuit reliably detects low for bus voltages < ~12V and high for bus voltages > ~22V.

See this repo for a good explanation on how the bus actually works: https://github.com/atc1441/TCSintercomArduino/blob/master/README.md


### Configuration
Get the proper message data by pressing the actual doorbell and door open buttons of your door control and looking at the log. E.g.
- `0x1100` is usually the generic open door command message.
- A doorbell message looks like `0x432180` where `0x4321` (`17185` in decimal, usually written on the back of the device) would be the address of the door control unit in your flat.

The following example configuration:
- creates a lock-entity in HomeAssistant which sends the configured open door command message upon "OPEN".
- creates a event-entity in HomeAssistant which would receive an event when one of the configure messages has been seen on the bus.
- change the input and output pins according to your hardware.

```YAML
logger:
  level: INFO
  
external_components:
  - source: my_components
    components: [tc_bus_transceiver]
    
lock:
  - platform: template
    id: door_house
    name: "Haustür"
    lambda: return LOCK_STATE_LOCKED;
    lock_action:
      logger.log: "locking not possible"
    unlock_action:
      - logger.log:
          format: "Opened house door."
          level: INFO
      - lambda: |-
          id(tcbustransceiver).send_message(2, 0x1100);
          id(door_house).publish_state(lock::LOCK_STATE_UNLOCKED);
          id(door_house).publish_state(lock::LOCK_STATE_LOCKED);

event:
  - platform: template
    id: doorbell
    name: "Klingel"
    event_types:
      - "frontdoor"
      - "flatdoor"
    device_class: "doorbell"

tc_bus_transceiver:
  id: tcbustransceiver
  input_pin: GPIO4
  output_pin: GPIO5
  on_message:
    then:
      - logger.log:
          format: "Received command: 0x%x"
          args: [ "message_data" ]
          level: INFO
      - if:
          condition:
            lambda: 'return (message_data == 0x432180);'
          then:
            - event.trigger:
                id: doorbell
                event_type: "frontdoor"
      - if:
          condition:
            lambda: 'return (message_data == 0x10432141);'
          then:
            - event.trigger:
                id: doorbell
                event_type: "flatdoor"
```
