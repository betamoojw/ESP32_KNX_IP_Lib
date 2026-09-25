#include "esp-knx-ip.h"
// Compile/link smoke test against the library in this checkout. Does not transmit.
void setup() {
    uint8_t bytes[8];
    knxip::dpt::encodeFloat16(-12.34f, bytes, sizeof(bytes));
    knxip::dpt::DateTime dt = {{2026,9,25},{5,12,0,0},0,0};
    knxip::dpt::encodeDateTime(dt, bytes, sizeof(bytes));
    if (WiFi.status() == WL_CONNECTED) {
        knx.start();
        knx.send_14byte_string(ESPKNXIP::GA_to_address(1,1,1), KNX_CT_WRITE, "KNX");
    }
}
void loop() { knx.loop(); }
