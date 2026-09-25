#include "esp-knx-ip.h"
#include <stdio.h>
#include <stdlib.h>
SerialStub Serial;
#ifdef ESP32
NetworkManager Network;
#else
WiFiStub WiFi;
#endif
EEPROMStub EEPROM;
uint32_t test_now = 100;
uint8_t NetworkUDP::incoming[600], NetworkUDP::outgoing[600];
size_t NetworkUDP::incoming_size = 0, NetworkUDP::outgoing_size = 0;
unsigned NetworkUDP::sends = 0;
bool NetworkUDP::fail = false;
uint16_t NetworkUDP::incoming_port = 3671, NetworkUDP::destination_port = 0;
IPAddress NetworkUDP::sender(192,168,1,20);
static unsigned checks=0, callbacks=0, discoveries=0;
static unsigned capacity_deliveries = 0;
static void capacity_callback(const message_t &, void *) { ++capacity_deliveries; }
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
static void callback(message_t const &msg, void *) {
    ++callbacks;
    CHECK(msg.received_on.bytes.high == 0x0a && msg.received_on.bytes.low == 3);
    CHECK(msg.source.bytes.high == 0x11 && msg.source.bytes.low == 1);
    CHECK(msg.ct == KNX_CT_WRITE && msg.data_len == 1 && msg.data[0] == 1);
}
static void discovery(const knxip::DiscoveryView &view, void *) { ++discoveries; CHECK(view.size == 58); }
static void receive(const uint8_t *data, size_t size, uint16_t port = 3671) {
    memcpy(NetworkUDP::incoming,data,size); NetworkUDP::incoming_size=size; NetworkUDP::incoming_port=port; knx.loop();
}
int main() {
    using knxip::Result;
#ifdef ESP32
    NetworkInterface ethernet;
    CHECK(knx.start_routing() == Result::NotConnected);
    CHECK(knx.discover(discovery) == Result::NotConnected);
    CHECK(knx.start_tunnel(IPAddress(192,168,1,20)) == Result::NotConnected);
    CHECK(NetworkUDP::sends == 0);
    Network.interface = &ethernet;
    ethernet.ready = false;
    CHECK(knx.start_routing() == Result::NotConnected);
    CHECK(knx.discover(discovery) == Result::NotConnected);
    CHECK(knx.start_tunnel(IPAddress(192,168,1,20)) == Result::NotConnected);
    ethernet.ready = true;
    ethernet.address = IPAddress();
    CHECK(knx.start_routing() == Result::NotConnected);
    ethernet.address = IPAddress(10,20,30,40);
#endif
    address_t ga = ESPKNXIP::GA_to_address(1,2,3);
    CHECK(ga.bytes.high == 0x0a && ga.bytes.low == 3);
    CHECK(knx.start_routing() == Result::Ok);
    auto id = knx.callback_register("receive",callback); knx.callback_assign(id,ga);
    const uint8_t golden[] = {6,0x10,5,0x30,0,17,0x29,0,0xbc,0xe0,0x11,1,0x0a,3,1,0,0x81};
    receive(golden,sizeof(golden)); CHECK(callbacks == 1);
    for (size_t n=0;n<sizeof(golden);++n) receive(golden,n);
    CHECK(callbacks == 1);
    uint8_t malformed[17]; memcpy(malformed,golden,17); malformed[14]=255; receive(malformed,17); CHECK(callbacks == 1);
    uint8_t huge[600]={}; receive(huge,600); CHECK(callbacks == 1);
    knx.write_1bit(ga,1); CHECK(knx.last_result() == Result::Ok);
    CHECK(NetworkUDP::outgoing_size == 17 && NetworkUDP::outgoing[16] == 0x81);
    knx.write_1bit(ga,0); CHECK(knx.last_result() == Result::Busy);
    test_now += 20;
    knx.send_3byte_time(ga,KNX_CT_WRITE,7,23,59,59);
    CHECK(knx.last_result() == Result::Ok && NetworkUDP::outgoing[17] == 0xf7);
    test_now += 20; knx.send_14byte_string(ga,KNX_CT_WRITE,"KNX");
    CHECK(knx.last_result() == Result::Ok && NetworkUDP::outgoing_size == 31 && NetworkUDP::outgoing[30] == 0);
    uint8_t ieee[] = {0,0xc1,0x48,0,0}; CHECK(knx.data_to_4byte_float(ieee) == -12.5f);
    uint8_t negative[] = {0,0x87,0xff}; CHECK(fabs(knx.data_to_2byte_float(negative)+.01f)<.00001f);
    const uint8_t *payload; size_t payload_size;
    message_t message = {}; message.ct=KNX_CT_WRITE;message.data=negative;message.data_len=3;
    CHECK(ESPKNXIP::message_payload(message,9,payload,payload_size) == Result::Ok);
    CHECK(payload_size == 2 && payload == negative+1);
    message.data_len=2; CHECK(ESPKNXIP::message_payload(message,9,payload,payload_size) == Result::InvalidLength);
    message.ct=KNX_CT_READ; CHECK(ESPKNXIP::message_payload(message,9,payload,payload_size) == Result::Unsupported);
    test_now += 20; uint8_t scene=63;
    CHECK(knx.send_dpt(ga,KNX_CT_WRITE,17,&scene,1) == Result::Ok);
    CHECK(NetworkUDP::outgoing_size == 18 && NetworkUDP::outgoing[16] == 0x80 && NetworkUDP::outgoing[17] == 63);
    scene=64; CHECK(knx.send_dpt(ga,KNX_CT_WRITE,17,&scene,1) == Result::InvalidValue);
    CHECK(knx.send_dpt(ga,KNX_CT_WRITE,999,&scene,1) == Result::Unsupported);
    test_now += 20; CHECK(knx.send_dpt(ga,KNX_CT_READ,9,nullptr,0) == Result::Ok);
    CHECK(NetworkUDP::outgoing_size == 17 && NetworkUDP::outgoing[16] == 0);
    uint8_t busy[] = {6,0x10,5,0x32,0,12,6,0,0,100,0,0}; receive(busy,12);
    test_now += 20; knx.write_1bit(ga,1); CHECK(knx.last_result() == Result::Busy);
    test_now += 200; knx.write_1bit(ga,1); CHECK(knx.last_result() == Result::Ok);
    uint8_t lost[] = {6,0x10,5,0x31,0,10,4,0,0,3}; receive(lost,10);
    CHECK(knx.diagnostics().routing_lost == 3 && knx.diagnostics().routing_busy == 1);
    CHECK(knx.discover(discovery) == Result::Ok); CHECK(knxip::read16(NetworkUDP::outgoing+2) == 0x0201);
#ifdef ESP32
    CHECK(NetworkUDP::outgoing[8] == 10 && NetworkUDP::outgoing[9] == 20 &&
          NetworkUDP::outgoing[10] == 30 && NetworkUDP::outgoing[11] == 40);
#endif
    uint8_t response[72] = {}; knxip::packetHeader(response,0x0202,72);
    knxip::Endpoint ep={{192,168,1,20},3671}; knxip::encodeHpai(response+6,ep);
    response[14]=54;response[15]=1;response[68]=4;response[69]=2;response[70]=4;response[71]=1;
    receive(response,72,3673); CHECK(discoveries == 1);
    test_now += 10000; knx.loop();
    CHECK(knx.describe(IPAddress(192,168,1,20),discovery) == Result::Ok);
    CHECK(knxip::read16(NetworkUDP::outgoing+2) == 0x0203);
    test_now += 10000; knx.loop();
#ifdef ESP32
    ethernet.address = IPAddress(172,16,2,3);
#endif
    CHECK(knx.start_tunnel(IPAddress(192,168,1,20)) == Result::Ok);
#ifdef ESP32
    CHECK(NetworkUDP::outgoing[8] == 172 && NetworkUDP::outgoing[9] == 16 &&
          NetworkUDP::outgoing[10] == 2 && NetworkUDP::outgoing[11] == 3);
    CHECK(memcmp(NetworkUDP::outgoing + 8, NetworkUDP::outgoing + 16, 4) == 0);
#endif
    CHECK(knx.tunnel_state() == knxip::Tunnel::State::Connecting);
    CHECK(knx.start_routing() == Result::Busy);
    CHECK(knx.discover(discovery,nullptr,3672) == Result::InvalidArgument);
    uint8_t connection[20] = {}; knxip::packetHeader(connection,0x0206,20); connection[6]=7;
    knxip::encodeHpai(connection+8,ep); connection[16]=4;connection[17]=4;connection[18]=0x11;connection[19]=0x0a;
    receive(connection,20,3672); CHECK(knx.tunnel_state() == knxip::Tunnel::State::Connected);
    knx.write_1bit(ga,1); CHECK(knx.last_result() == Result::Ok && NetworkUDP::outgoing[10] == 0x11);
    CHECK(NetworkUDP::outgoing[14] == 0x11 && NetworkUDP::outgoing[15] == 0x0a);
    uint8_t incoming[21] = {}; knxip::packetHeader(incoming,0x0420,21); incoming[6]=4;incoming[7]=7;
    memcpy(incoming+10,golden+6,11); receive(incoming,21,3672); CHECK(callbacks == 2);
    receive(incoming,21,3672); CHECK(callbacks == 2 && knxip::read16(NetworkUDP::outgoing+2) == 0x0421);
    CHECK(knx.disconnect_tunnel() == Result::Ok); test_now+=5000;knx.loop();
    CHECK(knx.tunnel_state() == knxip::Tunnel::State::Disconnected);
    // Local teardown cancels an in-flight connect and discovery without a live network.
    CHECK(knx.start_tunnel(IPAddress(192,168,1,20)) == Result::Ok);
    CHECK(knx.discover(discovery) == Result::Ok);
    knx.stop(); knx.stop();
    CHECK(knx.tunnel_state() == knxip::Tunnel::State::Disconnected);
    CHECK(!knx.tunnel_pending());
    CHECK(knx.send_checked(ga,KNX_CT_READ,0,nullptr) == Result::NotConnected);
    CHECK(knx.discover(discovery) == Result::Ok);
    knx.stop();
    NetworkUDP::fail = true;
    CHECK(knx.start_routing() == Result::IoError);
    NetworkUDP::fail = false;
    CHECK(knx.start_routing() == Result::Ok);
    // Duplicate assignment is idempotent; removal and restart preserve other configuration.
    for (unsigned i=0; i<MAX_CALLBACK_ASSIGNMENTS+1; ++i) knx.callback_assign(id,ga);
    knx.callback_unassign(id,ga);
    receive(golden,sizeof(golden)); CHECK(callbacks == 2);
    knx.callback_assign(id,ga);
    knx.stop(); CHECK(knx.start_routing() == Result::Ok);
    receive(golden,sizeof(golden)); CHECK(callbacks == 3);
    // Exercise the final valid ID, full subscription table, and persisted layout.
    ESPKNXIP capacity;
    capacity.load();
    for (unsigned i = 0; i < MAX_CALLBACKS; ++i)
        CHECK(capacity.callback_register("capacity", capacity_callback) == i);
    CHECK(capacity.callback_register("overflow", capacity_callback) == callback_id_t(-1));
    for (unsigned i = 0; i < MAX_CALLBACK_ASSIGNMENTS; ++i)
        capacity.callback_assign(callback_id_t(i % MAX_CALLBACKS), ESPKNXIP::GA_to_address(1,0,i));
    capacity.save_to_eeprom();
    CHECK(EEPROM.data[8] == MAX_CALLBACK_ASSIGNMENTS);
    const address_t last = ESPKNXIP::GA_to_address(1,0,MAX_CALLBACK_ASSIGNMENTS-1);
    const callback_id_t last_id = (MAX_CALLBACK_ASSIGNMENTS-1) % MAX_CALLBACKS;
    capacity.callback_unassign(last_id, last);
    capacity.restore_from_eeprom();
    CHECK(capacity.start_routing() == Result::Ok);
    memcpy(NetworkUDP::incoming, golden, sizeof(golden));
    NetworkUDP::incoming[12] = last.bytes.high;
    NetworkUDP::incoming[13] = last.bytes.low;
    NetworkUDP::incoming_size = sizeof(golden);
    capacity.loop();
    CHECK(capacity_deliveries == 1);
    capacity.stop();
    printf("PASS: %u Arduino client checks\n",checks);
}
