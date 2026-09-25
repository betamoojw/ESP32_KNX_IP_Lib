// Compile the actual example and exercise its network policy against driver stubs.
#include "../Examples/knx ip test/src/main.cpp"
#include <stdio.h>
#include <stdlib.h>
SerialStub Serial;
NetworkManager Network;
WiFiStub WiFi;
EthernetStub ETH;
ProvisioningStub WiFiProv;
EEPROMStub EEPROM;
uint32_t test_now = 0;
uint8_t NetworkUDP::incoming[600], NetworkUDP::outgoing[600];
size_t NetworkUDP::incoming_size = 0, NetworkUDP::outgoing_size = 0;
unsigned NetworkUDP::sends = 0;
bool NetworkUDP::fail = false;
uint16_t NetworkUDP::incoming_port = 3671, NetworkUDP::destination_port = 0;
IPAddress NetworkUDP::sender(192,168,1,20);
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
int main() {
    WiFi.STA.ready = ETH.ready = false;
    setup();
    CHECK(WiFiProv.starts == 1);
    loop(); CHECK(NetworkUDP::sends == 0);
    ETH.ready = true;
    test_now = 10000; loop();
    CHECK(Network.interface == &ETH && NetworkUDP::sends == 1);
    WiFi.STA.ready = true;
    test_now = 20000; loop();
    CHECK(Network.interface == &WiFi.STA && NetworkUDP::sends == 2);
    WiFi.STA.linked = false;
    test_now = 30000; loop();
    CHECK(Network.interface == &ETH && NetworkUDP::sends == 3);
    // Failed Wi-Fi retries must not interrupt a healthy Ethernet session.
    Network.event(ARDUINO_EVENT_WIFI_STA_DISCONNECTED, {});
    NetworkUDP::fail = true;
    ++test_now; loop(); CHECK(knx.last_result() != knxip::Result::IoError);
    NetworkUDP::fail = false;
    ETH.ready = false;
    test_now = 40000; loop();
    CHECK(NetworkUDP::sends == 3 && knx.last_result() == knxip::Result::NotConnected);
    ETH.ready = true;
    NetworkUDP::fail = true;
    loop(); CHECK(knx.last_result() == knxip::Result::IoError);
    NetworkUDP::fail = false;
    test_now += 1999; loop(); CHECK(NetworkUDP::sends == 3);
    ++test_now; loop(); CHECK(NetworkUDP::sends == 4);
    // A rapid loss/recovery to the same IP between loop calls must still rejoin.
    Network.event(ARDUINO_EVENT_ETH_DISCONNECTED, {});
    Network.event(ARDUINO_EVENT_ETH_GOT_IP, {});
    NetworkUDP::fail = true;
    loop(); CHECK(knx.last_result() == knxip::Result::IoError);
    NetworkUDP::fail = false;
    test_now += 2000; loop(); CHECK(knx.last_result() == knxip::Result::Ok);
    ETH.address = IPAddress(10,0,0,2);
    NetworkUDP::fail = true;
    loop(); CHECK(knx.last_result() == knxip::Result::IoError);
    NetworkUDP::fail = false;
    // Retry arithmetic survives millis wraparound.
    test_now = 0xffffff00u; loop();
    Network.event(ARDUINO_EVENT_ETH_LOST_IP, {});
    NetworkUDP::fail = true; loop();
    NetworkUDP::fail = false;
    test_now += 2000; loop(); CHECK(knx.last_result() != knxip::Result::IoError);
    printf("PASS: %u example network checks\n", checks);
}
