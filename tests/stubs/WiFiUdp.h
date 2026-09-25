#pragma once
#include "NetworkUdp.h"
class WiFiUDP : public NetworkUDP {
public:
    int beginMulticast(IPAddress, IPAddress group, uint16_t port) { return NetworkUDP::beginMulticast(group, port); }
    int beginPacketMulticast(IPAddress group, uint16_t port, IPAddress) { return beginPacket(group, port); }
    void flush() { clear(); }
};
