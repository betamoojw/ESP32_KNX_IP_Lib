# Network API migration review

Reviewed branch: `dev`, starting at `5244f14` (tracking `origin/master2`).

## Architecture and findings

The branch contains portable DPT codecs (`knx-codec.cpp`), KNXnet/IP encoding,
parsing and the tunnel state machine (`knx-protocol.cpp`), and an Arduino adapter
(`esp-knx-ip-client.cpp`) owning routing and discovery UDP sockets. Configuration,
EEPROM, callbacks and legacy typed helpers live in separate adapter source files.
The protocol implementation does not require a network-driver rewrite.

The transport had three migration issues:

1. The public ESP32 header imported WiFi and stored WiFiUDP sockets, unnecessarily
   coupling applications to the Wi-Fi driver.
2. Tunnel and discovery/description HPAIs used WiFi.localIP(), so Ethernet-only
   applications could advertise 0.0.0.0 or an unrelated Wi-Fi address.
3. The unpinned PlatformIO platform did not establish a core version with Network
   support. Host tests also modeled only the older ESP32 WiFiUDP path.

## Changes

ESP32 now requires core >= 3.0.0, imports Network, uses NetworkUDP for both sockets,
and clears receive data with clear(). The default NetworkInterface provides IPv4
readiness and the address advertised to KNX peers. Start and discovery operations
return NotConnected without transmitting when there is no usable address.
ESP8266 retains its Wi-Fi multicast signatures and flush() receive path.

The projects pin pioarduino 55.03.38 / Arduino-ESP32 3.3.8. Applications explicitly
include and initialize their chosen driver. Selection and reconnection rules are
in [the client guide](KNX_CLIENT_API.md). Protocol codecs and tunnel state-machine
logic are unchanged. There is no automatic failover or per-socket interface pinning.

## Validation

Host tests using Zig 0.16.0 passed: 348,981 protocol checks and 20,000 malformed-input
iterations, plus the ESP32 and ESP8266 adapter suites. The ESP32 stub exposes Network
without WiFi, tests missing interfaces/readiness/addresses, and verifies discovery
and tunnel HPAIs use the current default interface's address. Existing receive tests
exercise clear() on ESP32 and flush() on ESP8266.

Embedded compilation was attempted, but PlatformIO could not acquire the shared
packages.lock while another VS Code PlatformIO initialization was running. A bounded
lock diagnostic confirmed packages.lock times out even with a separate platform
directory. No successful embedded build or hardware KNX interoperability is claimed.
Rerun `pio run -e esp32 -e esp8266` and the Wi-Fi example build once the cache is free.
Ethernet multicast, tunnel replies and reconnect behavior still require hardware
validation on the intended KNX LAN.

## API references

- [Espressif Network API](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/network.html)
- [NetworkManager in core 3.0.0](https://github.com/espressif/arduino-esp32/blob/3.0.0/libraries/Network/src/NetworkManager.h)
- [NetworkUDP in core 3.3.0](https://github.com/espressif/arduino-esp32/blob/3.3.0/libraries/Network/src/NetworkUdp.h)
- [Pinned PlatformIO integration](https://github.com/pioarduino/platform-espressif32/releases/tag/55.03.38)
