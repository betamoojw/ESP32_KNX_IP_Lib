#ifndef KNX_CODEC_H
#define KNX_CODEC_H

#include <stdint.h>
#include <stddef.h>

namespace knxip {

enum class Result { Ok, InvalidArgument, InvalidLength, OutOfRange, InvalidValue,
                    Unsupported, Busy, NotConnected, IoError };

uint16_t read16(const uint8_t *p);
uint32_t read32(const uint8_t *p);
void write16(uint8_t *p, uint16_t v);
void write32(uint8_t *p, uint32_t v);

namespace dpt {
struct Layout { uint16_t bytes; uint8_t compactBits; bool variable; };

// Main-type wire layout only. Does not assert subtype-specific units or ranges.
Result layout(uint16_t mainType, Layout &value);
Result validate(uint16_t mainType, const uint8_t *data, size_t len);
// Payload bytes only: never include the APCI byte in these APIs.
// Integer helpers cover wire representations; subtype units/ranges remain explicit.
Result encodeUnsigned(uint64_t value, uint8_t *out, size_t bytes);
Result decodeUnsigned(const uint8_t *data, size_t bytes, uint64_t &value);
Result encodeSigned(int64_t value, uint8_t *out, size_t bytes);
Result decodeSigned(const uint8_t *data, size_t bytes, int64_t &value);
Result encodeCompact(uint8_t value, uint8_t bits, uint8_t *out);
Result decodeCompact(const uint8_t *data, size_t len, uint8_t bits, uint8_t &value);
Result encodeFloat16(float value, uint8_t *out, size_t capacity);
Result decodeFloat16(const uint8_t *data, size_t len, float &value);
Result encodeFloat32(float value, uint8_t *out, size_t capacity);
Result decodeFloat32(const uint8_t *data, size_t len, float &value);
// DPT5.001: fullScale=100; DPT5.003: fullScale=360.
Result encodeScaled(double value, double fullScale, uint8_t *out);
Result decodeScaled(const uint8_t *data, size_t len, double fullScale, double &value);
struct Time { uint8_t weekday, hour, minute, second; };
struct Date { uint16_t year; uint8_t month, day; };
struct DateTime {
    Date date;
    Time time;
    // F, WD, NWD, NY, ND, NDoW, NT, SUTI; CLQ/SRC in quality bits 7/6.
    uint8_t flags, quality;
};
Result encodeTime(const Time &value, uint8_t *out, size_t capacity);
Result decodeTime(const uint8_t *data, size_t len, Time &value);
Result encodeDate(const Date &value, uint8_t *out, size_t capacity);
Result decodeDate(const uint8_t *data, size_t len, Date &value);
Result encodeDateTime(const DateTime &value, uint8_t *out, size_t capacity);
Result decodeDateTime(const uint8_t *data, size_t len, DateTime &value);
Result encodeScene(uint8_t scene, bool learn, uint8_t *out); // DPT18, zero-based scene
Result decodeScene(const uint8_t *data, size_t len, uint8_t &scene, bool &learn);
Result encodeSceneInfo(uint8_t scene, bool inactive, uint8_t *out); // DPT26
Result decodeSceneInfo(const uint8_t *data, size_t len, uint8_t &scene, bool &inactive);
Result encodeStatusMode(uint8_t flags, uint8_t mode, uint8_t *out); // DPT6.020: mode 0..2
Result decodeStatusMode(const uint8_t *data, size_t len, uint8_t &flags, uint8_t &mode);
struct Access { uint32_t code; uint8_t flags, index; }; // DPT15: decimal code 0..999999
Result encodeAccess(const Access &value, uint8_t *out, size_t capacity);
Result decodeAccess(const uint8_t *data, size_t len, Access &value);
Result encodeString14(const char *value, uint8_t *out, size_t capacity, bool ascii = true);
Result decodeString14(const uint8_t *data, size_t len, char *out, size_t capacity, bool ascii = true);
// DPT24 (Latin-1) and DPT28 (UTF-8). Input size excludes the terminating NUL.
Result encodeString(const char *value, size_t size, uint8_t *out, size_t capacity, size_t &written, bool utf8 = true);
Result decodeString(const uint8_t *data, size_t len, char *out, size_t capacity, bool utf8 = true);
struct RGB { uint8_t red, green, blue; };
struct RGBW { uint8_t red, green, blue, white, valid; }; // valid: R=8 G=4 B=2 W=1
Result encodeRGB(const RGB &value, uint8_t *out, size_t capacity);
Result decodeRGB(const uint8_t *data, size_t len, RGB &value);
Result encodeRGBW(const RGBW &value, uint8_t *out, size_t capacity);
Result decodeRGBW(const uint8_t *data, size_t len, RGBW &value);
} // namespace dpt
} // namespace knxip
#endif
