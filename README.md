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

The library is under development. API may change multiple times in the future.

API documentation is available [here](https://github.com/envy/esp-knx-ip/wiki/API)

A simple example:

```c++
#include <esp-knx-ip.h>
#ifdef ESP32
#include <WiFi.h> // The application owns driver initialization.
#endif

const char* ssid = "my-ssid";  //  your network SSID (name)
const char* pass = "my-pw";    // your network password

config_id_t my_GA;
config_id_t param_id;

int8_t some_var = 0;

void setup()
{
	// Register a callback that is called when a configurable group address is receiving a telegram
  	knx.callback_register("Set/Get callback", my_callback);
	knx.callback_register("Write callback", my_other_callback);

	int default_val = 21;
	param_id = knx.config_register_int("My Parameter", default_val);

	// Register a configurable group address for sending out answers
	my_GA = knx.config_register_ga("Answer GA");

	knx.load(); // Try to load a config from EEPROM

	WiFi.begin(ssid, pass);
	while (WiFi.status() != WL_CONNECTED) {
		delay(500);
	}

	#ifdef ESP32
    Network.setDefaultInterface(WiFi.STA);
#endif
    knx.start(); // Start after the chosen network interface has an IPv4 address.
}

void loop()
{
	knx.loop();
}


void my_callback(message_t const &msg, void *arg)
{
	switch (msg.ct)
	{
	case KNX_CT_WRITE:
		// Save received data
		some_var = knx.data_to_1byte_int(msg.data);
		break;
	case KNX_CT_READ:
		// Answer with saved data
		knx.answer_1byte_int(msg.received_on, some_var);
		break;
	}
}

void my_other_callback(message_t const &msg, void *arg)
{
	switch (msg.ct)
	{
	case KNX_CT_WRITE:
		// Write an answer somewhere else
		int value = knx.config_get_int(param_id);
		address_t ga = knx.config_get_ga(my_GA);
		knx.answer_1byte_int(ga, (int8_t)value);
		break;
	}
}

```

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
