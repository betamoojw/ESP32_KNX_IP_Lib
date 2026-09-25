# ESP32-KNX-IP Without WEBSERVER

This is a ported fork of the original ESP8266 by @envy, all kudos to him
https://github.com/envy/esp-knx-ip

This is a library for the ESP32 to enable KNXnet/IP communication. It supports UDP multicast routing on 224.0.23.12:3671, discovery/description and UDP tunnelling.
It supports the Arduino platforms for ESP32 and ESP8266.

See the [implementation plan and review](docs/KNX_IMPLEMENTATION_PLAN.md) and [client API, DPT coverage and validation guide](docs/KNX_CLIENT_API.md). The current implementation is an application client; supported typed codecs and remaining subtype/compound limitations are listed explicitly in that guide.

## Prerequisities

ESP32 requires the official Arduino-ESP32 core **3.0.0 or newer**. The library uses
`NetworkUDP` and `Network.getDefaultInterface()`; it does not include or initialize
Wi-Fi or Ethernet drivers. ESP8266 retains its `WiFiUDP` transport.

The PlatformIO projects pin pioarduino `55.03.38`, which packages Arduino-ESP32
3.3.8. pioarduino is the PlatformIO integration, separate from Espressif's core.
Use the same platform URL in your application's environment:

```ini
[env:esp32]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.38/platform-espressif32.zip
board = esp32dev
framework = arduino
```

Initialize your chosen driver in the application (`WiFi.h`, `ETH.h`, etc.), wait
for an IPv4 address, then start KNX. For multiple active interfaces, select the
one connected to your KNX LAN using `Network.setDefaultInterface(WiFi.STA)` or
`Network.setDefaultInterface(ETH)` before starting. The library advertises that
interface's IPv4 address in discovery and tunnel HPAIs. Routing requires IPv4
multicast support on the chosen network. See [transport details](docs/KNX_CLIENT_API.md).

References: [Espressif Network API](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/network.html)
and [pioarduino 55.03.38](https://github.com/pioarduino/platform-espressif32/releases/tag/55.03.38).

## How to use

Use the [current client API guide](docs/KNX_CLIENT_API.md) for this fork. The
original upstream wiki does not describe its checked APIs or network lifecycle.

### Run the ESP32 example

Clone the development branch and build the included PlatformIO project:

```sh
git clone --branch dev https://github.com/betamoojw/ESP32_KNX_IP_Lib.git
cd ESP32_KNX_IP_Lib
pio run -d "Examples/knx ip test"
pio run -d "Examples/knx ip test" -t upload
pio device monitor -b 115200
```

Before uploading, match the Ethernet PHY/pin settings in the example's
`platformio.ini` to your board. The supplied setup uses a classic ESP32 with
4 MiB flash and an external LAN8720 PHY; an ESP32 development board alone does
not provide Ethernet. The example uses a larger application partition for
provisioning support.

On first boot, use Espressif's SoftAP provisioning app to configure Wi-Fi through
`PROV_KNX`; the demonstration AP password and proof of possession are both
`knx-setup`. Credentials are saved for subsequent boots. Ground GPIO4 during
application startup to clear Wi-Fi credentials and provision again. Customize
these settings for your device; see the [example guide](Examples/knx%20ip%20test/README.md).

The example prefers Wi-Fi with a usable IPv4 address, falls back to Ethernet,
and returns to Wi-Fi when it recovers. It automatically restarts KNX multicast
routing after network loss, interface changes or IP changes. Both interfaces
must reach the KNX multicast LAN. Its group addresses are:

| Group address | Function |
| --- | --- |
| `5/5/10` | DPT9 temperature, sent every 10 seconds; handles reads and writes |
| `5/5/11` | DPT1 switch controlling the GPIO2 LED |
| `5/1/16` | External DPT9 temperature, read every 15 seconds |

### Use it in your application

For another PlatformIO project, use the ESP32 platform configuration above and
add the library dependency:

```ini
lib_deps =
    https://github.com/betamoojw/ESP32_KNX_IP_Lib.git#dev
```

The `dev` branch follows ongoing changes; use a commit ref for reproducible builds.
For local development, use `symlink://<path-to-your-checkout>` instead. The included
example already uses a relative symlink to this checkout.

The following ESP32 sketch receives DPT1 writes at `1/1/1` and answers reads with
the saved value. Copy
[`NetworkConnection.h`](Examples/knx%20ip%20test/src/NetworkConnection.h) next to your
`main.cpp` and use the example's Ethernet build flags and partition settings. This
helper owns provisioning, interface selection and routing recovery; it is example
code, not part of the public library API.

```cpp
#include <Arduino.h>
#include <esp-knx-ip.h>
#include "NetworkConnection.h"

NetworkConnection network(knx);
const address_t switchGa = ESPKNXIP::GA_to_address(1, 1, 1);
uint8_t switchValue = 0;
bool answerPending = false;

void onSwitch(const message_t &message, void *) {
    if (message.ct == KNX_CT_READ) {
        answerPending = true;
    } else if (message.ct == KNX_CT_WRITE) {
        const uint8_t *payload;
        size_t size;
        if (ESPKNXIP::message_payload(message, 1, payload, size) == knxip::Result::Ok)
            switchValue = payload[0];
    }
}

void setup() {
    Serial.begin(115200);
    knx.physical_address_set(ESPKNXIP::PA_to_address(1, 1, 100));
    const callback_id_t id = knx.callback_register("Switch", onSwitch);
    if (id == callback_id_t(-1)) {
        Serial.println("Callback capacity exhausted");
        return;
    }
    knx.callback_assign(id, switchGa);
    network.begin();
}

void loop() {
    if (network.update()) {
        knx.loop();
        if (answerPending &&
            knx.send_dpt(switchGa, KNX_CT_ANSWER, 1, &switchValue, 1) == knxip::Result::Ok)
            answerPending = false; // Retry on a later loop if busy or sending failed.
    }
    delay(1);
}
```

Choose an individual address unique on your KNX installation and group addresses
with matching DPTs. To send a switch write, use
`knx.send_dpt(switchGa, KNX_CT_WRITE, 1, &switchValue, 1)` while connected and check
the result. Registering a callback alone does not subscribe it: always call
`callback_assign()` for each group address. Validate received payloads before
using them, and keep calling `loop()` frequently.

The helper above uses **routing**. For UDP tunnelling, initialize and select your
network interface, call `start_tunnel(serverIp)`, and wait for the asynchronous
connected state while calling `loop()`. After network loss, call `stop()` and
restart once the network is ready. Do not combine the routing helper with a tunnel
on the same client. See the [transport guide](docs/KNX_CLIENT_API.md#transport).
ESP8266 applications use their Wi-Fi driver and the same KNX APIs; the provisioning/
Ethernet helper is ESP32-specific. This fork has no browser configuration server.

## How to configure (buildtime)

Open the `esp-knx-ip.h` and take a look at the config options at the top inside the block marked `CONFIG`

## How to configure (runtime)

Configure the physical address with `physical_address_set()`, register callbacks with
`callback_register()`, and associate each callback with a group address using
`callback_assign()`. Configuration values can be persisted with `save_to_eeprom()`
and restored by `load()`. This fork has no web configuration server.

For checked sends, use `send_dpt()` or `send_payload()` and handle `Busy` by retrying
later. Existing void helpers report results through `last_result()`. Routing sends
are paced at 20 ms; tunnelling permits one outstanding telegram. See the API guide
for connection lifecycle and safe decoding examples.

## Build and test

```text
python tests/run_host.py --compiler g++
pio run -e esp32 -e esp8266
```

The root PlatformIO project builds this checkout. The existing example also uses
a local library symlink instead of downloading a different remote revision.

## Provisioned Wi-Fi / Ethernet example

See [the example guide](Examples/knx%20ip%20test/README.md) for Wi-Fi provisioning,
automatic Ethernet fallback, KNX routing recovery, and required PHY/pin settings.
