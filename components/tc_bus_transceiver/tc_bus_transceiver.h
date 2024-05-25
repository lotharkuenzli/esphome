#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"

namespace esphome {
namespace tc_bus_transceiver {

class MessageTrigger : public Trigger<uint32_t> {
  public:
    void process(uint32_t message_data) { this->trigger(message_data); }
};

class TcBusTransceiver : public Component {
  public:
    void set_input_pin(InternalGPIOPin  *pin) { pin_input = pin; }
    void set_output_pin(InternalGPIOPin  *pin) { pin_output = pin; }
    void setup() override;
    void loop() override;
    void send_message(uint32_t message_length, uint32_t message_data);
    void dump_config() override;
    
    void register_trigger(MessageTrigger *trig) { this->triggers_.push_back(trig); }
    InternalGPIOPin  *pin_input;
  
  protected:
    InternalGPIOPin  *pin_output;
    std::vector<MessageTrigger *> triggers_;
  
  private:
    void received_message(uint32_t message_length, uint32_t message_data);
    void run_receiver_statemachine(uint8_t new_bit_time);
    
    uint32_t receiver_bits_expected = 0;
    uint32_t receiver_bits_received = 0;
    uint32_t receiver_data_length = 0;
    uint32_t receiver_data = 0;
    bool receiver_checksum = (bool)0;
    
};

}  // namespace empty_component
}  // namespace esphome