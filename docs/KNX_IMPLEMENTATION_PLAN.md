# KNX DPT and KNXnet/IP implementation plan

Baseline: `dev`, commit `ed5a010`, reviewed 2026-09-25. Working tree was clean.

## Specification baseline and scope

* [KNX Association DPT specification v02.02.01](https://support.knx.org/hc/en-us/article_attachments/15392631105682), especially sections 1.4, 3 and the compound formats.
* [KNX specifications distribution and version information](https://support.knx.org/hc/en-us/articles/360000040999-KNX-Specifications). The public suite is v3; this repository does not contain that complete suite.
* [KNX Association tunnelling v01.05.03, public mirror](https://community-openhab-org.s3-eu-central-1.amazonaws.com/original/2X/8/8b3ec554f60872e37763d2005edc1c4c1fb16887.PDF).

Implement an Arduino application client over IPv4 UDP: multicast routing,
discovery/description client, and link-layer tunnelling client. Preserve the existing
callback representation (APCI low six bits at data[0], byte payload at data[1]).
New codecs operate on DPT payload bytes only and explicitly report errors.
Do not infer DPT from a received telegram: applications must configure it per GA.
The user confirmed this application-client scope during the review.

Full ETS device management, router/server profiles, KNX IP Secure/Data Secure,
TCP tunnelling, TP/RF/PL hardware drivers and certification are separate workstreams.
Do not advertise these as implemented. Likewise, a raw payload escape hatch is not
semantic support for all standardized DPT subtypes.

## Baseline review

| Priority | Finding | Required change |
|---|---|---|
| P1 | Receive casts unchecked UDP bytes to structs; VLA sized by untrusted datagram | Fixed buffers; validate every enclosing length before access |
| P1 | Header checks joined with AND; service compared in host byte order | Explicit network byte order and independent checks |
| P1 | Additional info and NPDU lengths unchecked | Checked cEMI parser, including additional-info TLVs |
| P1 | Negative DPT9 decoding wrong; unreachable alternate implementation | Signed 11-bit mantissa, bounded exponent, invalid-value handling |
| P1 | DPT14 numerically casts IEEE bits | Bit-preserving memcpy and network byte order |
| P2 | Time weekday shifted twice | Single field packing with range validation |
| P2 | DPT16 sender declared but not defined | Implement bounded zero-padded 14-byte string |
| P2 | Length-independent pointer conversion API | Add checked payload codec API; document legacy preconditions |
| P2 | Struct bitfields used as wire serializer; optional invalid checksum | Explicit bytes; no extra checksum in KNXnet/IP cEMI |
| P2 | No tunnelling/discovery implementation despite service enums | Client state machine and checked core-service decoding |
| P2 | Constructor memsets live String objects; persisted callback IDs unchecked | Normal C++ initialization and bounds checks |
| P2 | Example builds remote library, metadata ESP8266-only, README describes removed web UI | Local build validation and current documentation |

## Implementation sequence

1. Add portable, allocation-free byte utilities, DPT codecs, and cEMI framing.
   Cover compact fields, signed/unsigned integers, scaling/angle, KNX float,
   IEEE float, time/date, fixed/variable strings, scenes, date-time, 64-bit
   counters and RGB/RGBW. Explicit length/range/unsupported errors.
2. Replace routing send/receive with checked framing. Expose send result,
   source address, routing-lost/busy diagnostics and bounded backpressure.
3. Add UDP tunnelling: connect/CRD, assigned IA, endpoint validation, independent
   sequence counters, duplicate suppression, ACK/retry, heartbeat and disconnect.
   One outstanding telegram; return busy instead of silently dropping a queue.
4. Add search/description requests and bounded DIB response iteration; expose
   response data to applications without pretending to be a KNXnet/IP server.
5. Integrate existing convenience APIs; fix confirmed conversion defects.
6. Add deterministic tests: wire vectors, truncation at every boundary, invalid
   lengths/fields, DPT boundaries, floating-point representations, long frames,
   tunnel sequencing/timeouts and callback dispatch. Compile ESP32 and ESP8266.
7. Publish API examples, support matrix and actual validation results. Keep
   remaining conformance work visible rather than marking raw codecs complete.

## Completion gates

* Host tests execute the production C++ codecs/parser/state machine, not a Python reimplementation.
* Local-library ESP32 and ESP8266 builds succeed, or precise environment blockers are recorded.
* Malformed packets never dispatch callbacks; duplicate tunnel packets are acknowledged once per receipt but dispatched once.
* Wire vectors agree with documented network byte order and APDU compact/extended placement.
* Hardware acceptance remains explicit: ETS/group monitor round trips through a real router and tunnel interface, packet loss/reconnect, multicast flow control and Wi-Fi recovery.

## Full-support follow-on work

To claim *every* DPT subtype, maintain a versioned registry covering all identifiers,
units, ranges, reserved fields, invalid encodings and compound layouts in the
chosen complete specification suite. Test each independently against authoritative
vectors. HVAC/metering/access-control compounds require more than integer packing.
For full KNX medium/device conformance, select a device profile, implement its
management/object model and security requirements, then run the corresponding
KNX conformance suite and hardware interoperability tests.

## Execution record

Implemented:

* `knx-codec.h/.cpp`: portable codecs, main-type layout validation for 1..31,
  RGB/RGBW, checked dates/times, fixed/variable strings and exact 64-bit integers.
* `knx-protocol.h/.cpp`: bounded packet/cEMI parser and serializer, HPAI/DIB
  decoding, allocation-free UDP tunnel state machine.
* `esp-knx-ip-client.cpp`: Arduino transport integration, discovery socket,
  routing backpressure, checked send/receive payload APIs and diagnostics.
* Existing API fixes: DPT9/DPT14/time/string errors, no receive VLAs or wire
  struct casts, preserved callback layout plus source IA, guarded callback IDs.
* Local-source PlatformIO build projects, corrected ESP32/ESP8266 metadata,
  host regression runner and CI, and [API/support guide](KNX_CLIENT_API.md).

Validation on 2026-09-25:

* Host C++ tests: 348,981 assertions plus 20,000 exact-allocation malformed-input
  iterations; Arduino adapter: 49 checks. Passed with Zig 0.16.0 C++ compiler
  and `-Wall -Wextra -Werror`. Native sanitizer instrumentation was not verified;
  Linux GCC AddressSanitizer/UBSan CI is configured but was not executed here.
* Exhaustive decoding/re-encoding of every valid 16-bit KNX float encoding;
  independent signed-mantissa oracle and known wire vectors.
* Tunnel tests include endpoint mismatch, duplicate suppression, sequence rollover,
  malformed NPDU lengths, ACK timeout/error retries, confirmation ACKs, heartbeat
  expiry, server/client disconnect, NAT HPAI, send failure and timer wraparound.
* ESP32 compile/link passed with locally installed platform 2026.05.50,
  Arduino 3.3.8 and Xtensa GCC 15.2.0.
* ESP8266 compile/link passed with platform 4.2.1, Arduino 3.1.2 and GCC 10.3.0.
* Both final embedded builds completed without warnings. The existing
  `Examples/knx ip test` project also compiled and linked successfully against
  this checkout via `symlink://../..`.

Outstanding acceptance and coverage:

* No physical KNX/ETS hardware was used. Test actual routing and tunnelling,
  multicast flow control, loss/reconnect and NAT before production deployment.
* Main-type codec coverage is not a complete subtype semantic registry. The
  support guide lists which constraints are checked and which remain application
  responsibilities. Specialized compound DPTs outside that matrix remain to be
  implemented and independently tested; raw transport is available but is not
  counted as typed support.
* The whole current KNX v3 conformance suite was not available in the repository.
  No claim of KNX certification or full v3 device-profile compliance is made.
