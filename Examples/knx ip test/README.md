# Example: Wi-Fi provisioning and Ethernet fallback

Build/upload from this directory with `pio run -t upload`; monitor at 115200 baud.
The pinned platform uses Arduino-ESP32 3.3.8. The example requires a classic ESP32
with 4 MiB flash and an external Ethernet PHY. `esp32dev` alone has no Ethernet PHY.

## Hardware configuration

The example `platformio.ini` configures LAN8720, PHY address 0, MDC GPIO23,
MDIO GPIO18, external RMII clock on GPIO0, and no software-controlled PHY power pin.
Match these settings to your board before flashing. Other PHYs (including SPI
Ethernet) need their own ETH pin definitions and, where applicable, SPI setup.
GPIO2 drives the LED. GPIO4 is the provisioning reset input; change it in
`NetworkConnection.h` if your board uses it. Do not use GPIO0 for provisioning reset:
it is the RMII clock in this configuration.

Provisioning increases firmware size. `min_spiffs.csv` provides two larger app
slots on 4 MiB flash with a smaller filesystem. Upload the partition table along
with the firmware when migrating from a different layout.

## Provisioning

1. On first boot, use Espressif's ESP SoftAP Provisioning app to provision Wi-Fi.
2. Select `PROV_KNX`. The example AP password and security-1 proof of possession
   are both `knx-setup`. These are demonstration values; customize them per device.
3. Submit the SSID/password. Credentials persist in the ESP32 Wi-Fi NVS storage;
   later boots reconnect without provisioning again.
4. To replace credentials (including after an incorrect password), ground GPIO4
   while the application starts, then release it. This clears saved Wi-Fi credentials
   and reopens provisioning. It does not erase KNX configuration.

This uses Espressif's provisioning protocol, not a browser captive portal. Provisioning
runs asynchronously, so Ethernet and KNX remain available while Wi-Fi is configured.
A temporary Wi-Fi outage does not erase credentials or force provisioning.

## Connection policy

`NetworkConnection` owns driver setup, provisioning, default-interface selection,
and multicast session recovery. `KnxExample` owns group addresses, callbacks, LED
behavior and periodic telegrams. The library owns UDP/protocol state. No driver
inheritance hierarchy is needed: composition keeps those responsibilities separate.

A connected Wi-Fi station with IPv4 is preferred. Otherwise, Ethernet is selected
as soon as its link and IPv4 are ready. With neither available, the loop keeps
running without sending KNX. Wi-Fi automatically reconnects in the background.
When it becomes usable again, the example returns to Wi-Fi.

On network events, interface changes, or address changes, the example stops the old
KNX session and rejoins routing multicast on the selected interface. Failed starts
retry every two seconds. Events also catch a disconnect/reconnect to the same IP
between loop iterations. Event callbacks only signal the main loop; they never
manipulate KNX sockets from the network event task.

This example uses KNXnet/IP **routing**, not a unicast tunnel. Both networks must
reach the KNX multicast LAN. Routing has no server handshake; successful startup
means the multicast socket opened, not that a KNX router confirmed connectivity.
Core routing rules still apply when interfaces have overlapping subnets.

Temperature uses DPT9 at 5/5/10; the LED switch uses DPT1 at 5/5/11; an external
DPT9 read targets 5/1/16. Temperature writes occur every 10 seconds and external
reads every 15 seconds. Busy sends are retried. Received values are validated before
decoding. `setTemperatureAddress()` removes the old subscription before assigning
the new one; the former timed address-change/removal demo has been removed.

## Validation on hardware

After provisioning, check Wi-Fi telegrams, disable the access point with Ethernet
connected, then restore Wi-Fi. Repeat with both links down, Ethernet restored first,
DHCP address changes, and a quick cable disconnect/reconnect. Verify real telegrams
with a KNX monitor; host tests cannot validate PHY wiring or multicast propagation.

References: [Espressif Network API](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/network.html)
and [pinned provisioning API](https://github.com/espressif/arduino-esp32/blob/3.3.8/libraries/WiFiProv/src/WiFiProv.h).
