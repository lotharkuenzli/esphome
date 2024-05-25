#include "esphome/core/log.h"
#include "esphome.h"
#include "tc_bus_transceiver.h"

namespace esphome {
namespace tc_bus_transceiver {

#define OUTPUT_IS_ACTIVE            sender_pin_output.digital_read()
#define OUTPUT_SET_INACTIVE         sender_pin_output.digital_write(false)
#define OUTPUT_SET_ACTIVE           sender_pin_output.digital_write(true)

#define TIMER_TICKS_ONE_MS          0.5 // timer runs every 2ms

static const char *TAG = "tc_bus_transceiver";

// data owned by ISR
static const uint32_t receiver_bit_times_size = 64;
uint32_t receiver_micros_prev = 0;
uint32_t sender_bit_times_position = 0;
uint32_t sender_wait_timer_cycles = 0;
ISRInternalGPIOPin sender_pin_output;

// used for communication ISR -> user context
uint32_t receiver_bit_times_head = 0;
uint8_t receiver_bit_times_01ms[receiver_bit_times_size];

// used for communication user context -> ISR
uint8_t sender_bit_times_1ms[64];

// data owned by user_context
uint32_t receiver_bit_times_tail = 0;

void IRAM_ATTR timer1ISR() {
    if(sender_wait_timer_cycles > 0) {
        sender_wait_timer_cycles--;
    }
    if(sender_wait_timer_cycles == 0) {
        sender_wait_timer_cycles = sender_bit_times_1ms[sender_bit_times_position];
        sender_bit_times_position++;
        if(sender_wait_timer_cycles > 0) {
            if(OUTPUT_IS_ACTIVE) {
                OUTPUT_SET_INACTIVE;
            }
            else {
                OUTPUT_SET_ACTIVE;
            }
        }
        else {
            OUTPUT_SET_INACTIVE;
            sender_bit_times_position = 0;
            // we're done sending, disable timer
            timer1_disable();
        }
    }
}

void IRAM_ATTR inputISR(TcBusTransceiver *) {
    uint32_t micros_now = micros();
    uint32_t elapsed_01ms = (micros_now - receiver_micros_prev) / 100;

    receiver_micros_prev = micros_now;
    if(10 < elapsed_01ms && elapsed_01ms < 200) {
        receiver_bit_times_01ms[receiver_bit_times_head] = elapsed_01ms;
        receiver_bit_times_head++;
        receiver_bit_times_head &= receiver_bit_times_size - 1;
    }
}

void TcBusTransceiver::setup() {
    ESP_LOGI(TAG, "setup");
    
    sender_pin_output = this->pin_output->to_isr();
    this->pin_output->pin_mode(gpio::FLAG_OUTPUT);
    
    OUTPUT_SET_INACTIVE;
    
    this->pin_input->pin_mode(gpio::FLAG_INPUT | gpio::FLAG_PULLUP);
    this->pin_input->attach_interrupt(inputISR, this, gpio::INTERRUPT_ANY_EDGE);
    
    timer1_attachInterrupt(timer1ISR);
    timer1_write(10000); // 10000 / 5 ticks/µs => 2ms interval
}

void TcBusTransceiver::loop() {
    uint32_t receiver_bit_times_head_snapshot = receiver_bit_times_head;
    
    if(receiver_bit_times_tail != receiver_bit_times_head_snapshot) {
        ESP_LOGD(TAG, "t: %i", receiver_bit_times_01ms[receiver_bit_times_tail]);
        this->run_receiver_statemachine(receiver_bit_times_01ms[receiver_bit_times_tail]);
        receiver_bit_times_tail++;
        receiver_bit_times_tail &= receiver_bit_times_size - 1;
    }
}

void TcBusTransceiver::send_message(uint32_t message_length, uint32_t message_data) {
    ESP_LOGI(TAG, "send_message, len=%i, data=0x%x", message_length, message_data);
    
    uint32_t bit_times_position = 0;
    uint32_t data_bits_to_send = message_length * 8;
    bool checksum = (bool)1;
    // start bit
    sender_bit_times_1ms[bit_times_position++] = 6 * TIMER_TICKS_ONE_MS;
    // length bit
    if(message_length == 2) {
        sender_bit_times_1ms[bit_times_position++] = 2 * TIMER_TICKS_ONE_MS;
    }
    else if(message_length == 4) {
        sender_bit_times_1ms[bit_times_position++] = 4 * TIMER_TICKS_ONE_MS;
    }
    // data bits
    while(data_bits_to_send > 0) {
        data_bits_to_send--;
        bool next_bit = (bool)((message_data >> data_bits_to_send) & 0x1);
        if(next_bit) {
            sender_bit_times_1ms[bit_times_position++] = 4 * TIMER_TICKS_ONE_MS;
            checksum ^= (bool)1;
        }
        else {
            sender_bit_times_1ms[bit_times_position++] = 2 * TIMER_TICKS_ONE_MS;
            checksum ^= (bool)0;
        }
    }
    // checksum bit
    if(checksum) {
        sender_bit_times_1ms[bit_times_position++] = 4 * TIMER_TICKS_ONE_MS;
    }
    else {
        sender_bit_times_1ms[bit_times_position++] = 2 * TIMER_TICKS_ONE_MS;
    }
    // make sure last time in sequence is 0
    sender_bit_times_1ms[bit_times_position++] = 0;
    
    // start sender by enabling timer
    timer1_enable(TIM_DIV16, TIM_EDGE, TIM_LOOP);
}

void TcBusTransceiver::received_message(uint32_t message_length, uint32_t message_data) {
    ESP_LOGI(TAG, "received_message, len=%i, data=0x%x", message_length, message_data);

    // fire registered triggers
    for (auto *trig : this->triggers_)
          trig->process(message_data);
}

void TcBusTransceiver::run_receiver_statemachine(uint8_t new_bit_time) {

    // received zero bit (1ms < x < 3ms)
    if(10 < new_bit_time && new_bit_time < 30 && this->receiver_bits_expected > 0) {
        if(this->receiver_bits_received == 0) {
            this->receiver_bits_expected = 18;
            this->receiver_data_length = 2;
            ESP_LOGD(TAG, "dl0");
        }
        else if(this->receiver_bits_received == this->receiver_bits_expected - 1) {
            ESP_LOGD(TAG, "cs0-%i", this->receiver_checksum);
            if(this->receiver_checksum == (bool)0) {
                this->received_message(this->receiver_data_length, this->receiver_data);
            }
        }
        else {
            this->receiver_data = (this->receiver_data << 1) | 0;
            this->receiver_checksum ^= (bool)0;
            ESP_LOGD(TAG, "d0");
        }
        this->receiver_bits_received++;
    }
    // received one bit (3ms < x < 5ms)
    else if(30 < new_bit_time && new_bit_time < 50 && this->receiver_bits_expected > 0) {
        if(this->receiver_bits_received == 0) {
            this->receiver_bits_expected = 34;
            this->receiver_data_length = 4;
            ESP_LOGD(TAG, "dl1");
        }
        else if(this->receiver_bits_received == this->receiver_bits_expected - 1) {
            ESP_LOGD(TAG, "cs1-%i", this->receiver_checksum);
            if(this->receiver_checksum == (bool)1) {
                this->received_message(this->receiver_data_length, this->receiver_data);
            }
        }
        else {
            this->receiver_data = (receiver_data << 1) | 1;
            this->receiver_checksum ^= (bool)1;
            ESP_LOGD(TAG, "d1");
        }
        this->receiver_bits_received++;
    }
    // received start bit (5ms < x < 7ms)
    else if(50 < new_bit_time && new_bit_time < 70) {
        this->receiver_bits_expected = 1;
        this->receiver_bits_received = 0;
        this->receiver_data = 0;
        this->receiver_checksum = (bool)1;
        ESP_LOGD(TAG, "sb");
    }
    // received crap
    else {
        this->receiver_bits_received = 0;
        this->receiver_data = 0;
    }
}

void TcBusTransceiver::dump_config(){
    ESP_LOGCONFIG(TAG, "Empty component");
}

}  // namespace empty_component
}  // namespace esphome