# KNX application-client API and coverage

## Transport

Call `loop()` frequently (without long blocking work). Start after Wi-Fi has an IP.
`start()` retains the multicast-routing default; `start_routing()` returns a result.
Use `start_tunnel(IPAddress(192,168,1,20))` instead to connect to an interface.
This is asynchronous: wait for `tunnel_state() == knxip::Tunnel::State::Connected`.
The server-assigned individual address is available through `tunnel_address()`;
it is used on tunnel telegrams without changing the configured routing address.

`disconnect_tunnel()` starts a graceful disconnect. Continue calling `loop()` until
disconnected before changing transports. A rejected connection, failed heartbeat,
or exhausted ACK retry ends the connection. The application can call
`start_tunnel()` again with its own reconnect/backoff policy after it is disconnected.
After a Wi-Fi reconnect, call the appropriate start function again once the old
tunnel is disconnected. No automatic interface selection or reconnect is implied.

| Service | Behavior |
|---|---|
| Routing indication | GroupValue read/response/write; cEMI L_Data.ind; standard and extended frames |
| Routing lost | Validated and counted by `diagnostics().routing_lost` |
| Routing busy | Validated wait time, randomized backoff, decaying busy factor; sends return Busy |
| Routing rate | At least 20 ms between sent indications (at most 50/s) |
| Search/description | Legacy IPv4/UDP requests; HPAI and DIB validation; response callback |
| Tunnel connection | Link-layer CRI, returned CRD/IA, negotiated data endpoint, optional route-back HPAI (`nat=true`) |
| Tunnel send | L_Data.req, one outstanding request, 1 s ACK timeout, one identical retry, then disconnect |
| Tunnel receive | L_Data.ind dispatched; L_Data.con ACKed; duplicate suppression; separate modulo-256 counters |
| Tunnel heartbeat | Every 60 s, 10 s response timeout, three attempts, then disconnect |
| Disconnect | Client/server initiated; 5 s response timeout |

`send_payload()` and `send_dpt()` return `knxip::Result`. `Ok` means a datagram was
accepted by the socket (or is pending a tunnel ACK), **not** that a bus device acted
on it. Check `tunnel_pending()`, `tunnel_state()` and `tunnel_status()` for tunnel
progress. `last_result()` records the most recent public operation, not asynchronous
events. `Busy` means retry later; there is no hidden queue. Legacy void send/write/
answer helpers expose their result through `last_result()` and may also return Busy.

The receive buffer is bounded to 530 bytes; supported group payloads are at most
254 bytes. Larger datagrams and malformed lengths are discarded. A peer/router
may impose a smaller APDU limit; choose payload sizes supported by the installation.
No segmentation is provided. Only unnumbered group application services are dispatched.

## DPT values

`knxip::dpt` codecs operate on payload bytes **without** APCI. Encoders take a buffer
capacity; decoders require the exact payload length and return errors instead of
reading past the supplied buffer. `encodeUnsigned`/`encodeSigned` take the desired
wire byte count (1..8); the caller must supply that many bytes.

```cpp
uint8_t payload[2];
auto result = knxip::dpt::encodeFloat16(-12.34f, payload, sizeof(payload));
if (result == knxip::Result::Ok) {
    result = knx.send_dpt(ESPKNXIP::GA_to_address(1, 2, 3),
                          KNX_CT_WRITE, 9, payload, sizeof(payload));
}
// Keep/retry the value later if result == knxip::Result::Busy.
```

`send_dpt()` validates the main-type layout and selects compact/byte payload
placement automatically. `send_payload()` is an escape hatch for explicitly encoded
payloads; `compact=true` embeds a 1..6-bit value into the APCI byte. It must not be
used for an 8-bit DPT even when its current value happens to fit into six bits.

| Main types | Codec / representation | Validation boundary |
|---|---|---|
| 1, 2, 3, 23, 31 | encode/decodeCompact, 1/2/4/2/3 bits | Bit width; subtype enum restrictions remain with application |
| 4, 5, 7, 12 | encode/decodeUnsigned, 1/1/2/4 bytes | Integer range; character set/subtype units remain with application |
| 5.001, 5.003 | encode/decodeScaled, fullScale 100 or 360 | Finite input and range; nearest wire value |
| 6, 8, 13, 29 | encode/decodeSigned, 1/2/4/8 bytes | Signed range, exact 64-bit values |
| 6.020 | encode/decodeStatusMode | Five raw flag bits plus one-hot mode 0..2 |
| 9 | encode/decodeFloat16 | Signed mantissa, exponent, finite/range checks; 0x7fff invalid |
| 10, 11 | encode/decodeTime, encode/decodeDate | Calendar/time validity; full year 1990..2089 for Date |
| 14 | encode/decodeFloat32 | IEEE-754 bits preserved, including nonfinite encodings; application decides whether to use them |
| 15 | encode/decodeAccess | Six BCD digits, four flags and index |
| 16 | encode/decodeString14 | 14 bytes, zero padding; ASCII or Latin-1, no transcoding |
| 17 | encodeUnsigned(scene,buffer,1), send_dpt main 17 | Zero-based 0..63; byte payload, never compact |
| 18, 26 | encode/decodeScene, encode/decodeSceneInfo | Scene 0..63; command/inactive flags and reserved bit |
| 19 | encode/decodeDateTime | Year 1900..2155, validity flags, fault/quality flags, 24:00:00 |
| 20, 21, 22, 25, 27, 30 | encode/decodeUnsigned, 1/1/2/1/4/3 bytes | Wire enum/bitset/nibble representation; subtype semantics remain with application |
| 24, 28 | encode/decodeString (utf8=false/true) | NUL termination, explicit size, UTF-8 validation |
| 232.600, 251.600 | encode/decodeRGB, encode/decodeRGBW | RGB channels; RGBW reserved bytes/validity mask |

`layout()` and `validate()` describe these main types. Main-type validation cannot
validate a subtype: for example 6.020 needs `decodeStatusMode()` while 6.010 is
signed; 4.001 additionally requires ASCII; 9.001 has temperature-specific limits.
Subtype engineering units, scale factors beyond the explicit scaled codec, enum
names and application constraints are not a complete versioned DPT registry.
Specialized HVAC/metering compounds beyond the table require explicit payload
encoding through `send_payload()`; they are **not** claimed as typed DPT support.

KNX group telegrams carry no DPT identifier. Configure the DPT for each GA in your
application. Do not guess it from payload length.

## Receiving and compatibility

Existing callbacks retain `message_t.data[0]` as the compact value (or zero for byte
payloads); extended payload starts at `data[1]`. `data_len` includes that first byte.
`received_on` is the destination GA and `source` is the sender's individual address.
All message/DIB pointers expire when their callback returns. Copy them if needed.

```cpp
void temperature(message_t const &msg, void *) {
    const uint8_t *payload;
    size_t size;
    if (ESPKNXIP::message_payload(msg, 9, payload, size) != knxip::Result::Ok)
        return; // Read requests have no value; respond separately if desired.
    float value;
    if (knxip::dpt::decodeFloat16(payload, size, value) == knxip::Result::Ok) {
        // Use value after applying your configured subtype/application constraints.
    }
}
```

Legacy `data_to_*` pointer APIs cannot check allocation length; callers must check
`msg.data_len` first, or use `message_payload` and the checked decoders. The misspelled
`data_to_3byte_data` remains; `data_to_3byte_date` is its correctly named alias.
Legacy date helpers continue to use the wire year 0..99. New Date uses the full year.
DPT16 sends reject strings longer than 14 characters instead of truncating them.

## Discovery

```cpp
void found(const knxip::DiscoveryView &device, void *) {
    size_t length;
    const uint8_t *families = knxip::findDib(device, 2, length);
    // device.control is the advertised IPv4 control endpoint.
    // families contains validated service-family/version pairs after its 2-byte header.
    (void)families;
}
// With Wi-Fi connected:
// knx.discover(found);
// or knx.describe(IPAddress(192,168,1,20), found);
```

Search stays active for 10 s and can report multiple devices. Description finishes
on its first valid response or after 10 s. The separate discovery socket defaults
to local port 3673; the tunnel socket defaults to 3672. Port conflicts are rejected.
Search v2, extended DIB negotiation, TCP, secure services, ETS management and server
roles are not implemented. The library never advertises unsupported server services.

## Validation

Run `python tests/run_host.py --compiler g++` (or Clang). A Zig installation is also
supported: `--compiler .tools/ziglang/zig.exe`. With GCC or Clang supporting
the runtimes, add `--sanitize` (not supported by this runner with Zig).
Tests compile the production C++ and a mocked Arduino
adapter; no KNX network traffic is generated by these tests.

Run `pio run -e esp32 -e esp8266` from the repository root for local-source embedded
compile/link smoke tests. Real-device/ETS validation is still required for multicast
network behavior, flow-control interoperability, NAT deployment, loss/reconnect and
peer APDU limits. Passing these tests is not KNX certification.
