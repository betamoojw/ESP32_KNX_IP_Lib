#pragma once
#include "IPAddress.h"
#ifdef ESP32
#include "Network.h"
struct WiFiStub {
    NetworkInterface STA;
    void setAutoReconnect(bool) {}
};
#else
struct WiFiStub { IPAddress localIP() { return IPAddress(192,168,1,10); } };
#endif
extern WiFiStub WiFi;
