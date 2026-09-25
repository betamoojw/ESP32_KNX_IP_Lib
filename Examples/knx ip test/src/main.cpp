#include <Arduino.h>
#include <esp-knx-ip.h>
#include "NetworkConnection.h"

class KnxExample {
public:
    explicit KnxExample(ESPKNXIP &client) : client_(client), network_(client) {}

    void begin() {
        Serial.begin(115200);
        pinMode(LedPin, OUTPUT);
        client_.physical_address_set(ESPKNXIP::PA_to_address(1, 0, 101));
        callback_ = client_.callback_register("Example", receive, this);
        client_.callback_assign(callback_, temperatureGa_);
        client_.callback_assign(callback_, switchGa_);
        client_.callback_assign(callback_, externalGa_);
        network_.begin();
    }

    void update() {
        if (!network_.update()) return;
        client_.loop();
        const uint32_t now = millis();
        if (uint32_t(now - temperatureAt_) >= 10000) {
            client_.write_2byte_float(temperatureGa_, temperature_);
            if (client_.last_result() == knxip::Result::Ok) temperatureAt_ = now;
        }
        if (uint32_t(now - externalAt_) >= 15000) {
            client_.send_ext(externalGa_);
            if (client_.last_result() == knxip::Result::Ok) externalAt_ = now;
        }
    }

    void setTemperatureAddress(address_t address) {
        client_.callback_unassign(callback_, temperatureGa_);
        temperatureGa_ = address;
        client_.callback_assign(callback_, temperatureGa_);
    }

private:
    static void receive(const message_t &message, void *context) {
        static_cast<KnxExample *>(context)->receive(message);
    }
    void receive(const message_t &message) {
        const bool temperature = message.received_on.value == temperatureGa_.value;
        const bool external = message.received_on.value == externalGa_.value;
        if (temperature && message.ct == KNX_CT_READ) {
            client_.answer_2byte_float(temperatureGa_, temperature_);
            return;
        }
        const uint8_t *payload;
        size_t size;
        if ((temperature && message.ct == KNX_CT_WRITE) || (external && message.ct == KNX_CT_ANSWER)) {
            if (ESPKNXIP::message_payload(message, 9, payload, size) != knxip::Result::Ok) return;
            float value;
            if (knxip::dpt::decodeFloat16(payload, size, value) != knxip::Result::Ok) return;
            if (temperature) temperature_ = value;
            else { Serial.print("External temperature: "); Serial.println(value); }
        } else if (message.received_on.value == switchGa_.value && message.ct == KNX_CT_WRITE) {
            if (ESPKNXIP::message_payload(message, 1, payload, size) == knxip::Result::Ok)
                digitalWrite(LedPin, payload[0] ? HIGH : LOW);
        }
    }
    static constexpr uint8_t LedPin = 2;
    ESPKNXIP &client_;
    NetworkConnection network_;
    callback_id_t callback_ = 0;
    address_t temperatureGa_ = ESPKNXIP::GA_to_address(5, 5, 10);
    address_t switchGa_ = ESPKNXIP::GA_to_address(5, 5, 11);
    address_t externalGa_ = ESPKNXIP::GA_to_address(5, 1, 16);
    float temperature_ = 21.5f;
    uint32_t temperatureAt_ = 0, externalAt_ = 0;
};

KnxExample example(knx);
void setup() { example.begin(); }
void loop() { example.update(); delay(1); }
