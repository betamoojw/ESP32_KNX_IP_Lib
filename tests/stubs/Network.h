#pragma once
#include "IPAddress.h"
enum arduino_event_id_t {
    ARDUINO_EVENT_WIFI_STA_DISCONNECTED, ARDUINO_EVENT_WIFI_STA_LOST_IP,
    ARDUINO_EVENT_WIFI_STA_GOT_IP, ARDUINO_EVENT_ETH_DISCONNECTED,
    ARDUINO_EVENT_ETH_LOST_IP, ARDUINO_EVENT_ETH_GOT_IP
};
struct arduino_event_info_t {};
struct NetworkInterface {
    IPAddress address = IPAddress(10,20,30,40);
    bool ready = true;
    bool linked = true;
    bool connected() const { return linked; }
    bool hasIP() const { return ready; }
    IPAddress localIP() const { return address; }
};
struct NetworkManager {
    void begin() {}
    void setHostname(const char *) {}
    bool setDefaultInterface(NetworkInterface &value) { interface = &value; return true; }
    void (*event)(arduino_event_id_t, arduino_event_info_t) = nullptr;
    template<class Callback> void onEvent(Callback callback) {
        static Callback saved(callback);
        event = [](arduino_event_id_t id, arduino_event_info_t info) { saved(id, info); };
    }
    NetworkInterface *interface = nullptr;
    NetworkInterface *getDefaultInterface() { return interface; }
};
extern NetworkManager Network;
