#ifndef TEST_UDP_H
#define TEST_UDP_H
#include "IPAddress.h"
class NetworkUDP {
    uint16_t port_ = 0;
public:
    static uint8_t incoming[600], outgoing[600];
    static size_t incoming_size, outgoing_size;
    static unsigned sends;
    static bool fail;
    static uint16_t incoming_port, destination_port;
    static IPAddress sender;
    int begin(uint16_t port) { port_ = port; return !fail; }
    int beginMulticast(IPAddress, uint16_t port) { return begin(port); }
    int beginMulticastPacket() { outgoing_size = 0; return !fail; }
    int beginPacket(IPAddress, uint16_t port) { destination_port = port; outgoing_size = 0; return !fail; }
    void stop() { port_ = 0; }
    size_t write(const uint8_t *data, size_t n) { memcpy(outgoing,data,n); outgoing_size = n; return n; }
    int endPacket() { ++sends; return !fail; }
    int parsePacket() { return port_ == incoming_port ? int(incoming_size) : 0; }
    int read(uint8_t *out, size_t len) { if (len > incoming_size) len = incoming_size; memcpy(out,incoming,len); return int(len); }
    void clear() { incoming_size = 0; }
    IPAddress remoteIP() { return sender; }
    uint16_t remotePort() { return 3671; }
};
#endif
