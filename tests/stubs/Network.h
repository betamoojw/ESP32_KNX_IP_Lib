#pragma once
#include "IPAddress.h"
struct NetworkInterface {
    IPAddress address = IPAddress(10,20,30,40);
    bool ready = true;
    bool hasIP() const { return ready; }
    IPAddress localIP() const { return address; }
};
struct NetworkManager {
    NetworkInterface *interface = nullptr;
    NetworkInterface *getDefaultInterface() { return interface; }
};
extern NetworkManager Network;
