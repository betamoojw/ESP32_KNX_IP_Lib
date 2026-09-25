#include "knx-codec.h"
#include <math.h>
#include <string.h>
#include <limits.h>

namespace knxip {
uint16_t read16(const uint8_t *p) { return uint16_t((uint16_t(p[0]) << 8) | p[1]); }
uint32_t read32(const uint8_t *p) { return (uint32_t(read16(p)) << 16) | read16(p + 2); }
void write16(uint8_t *p, uint16_t v) { p[0] = uint8_t(v >> 8); p[1] = uint8_t(v); }
void write32(uint8_t *p, uint32_t v) { write16(p, uint16_t(v >> 16)); write16(p + 2, uint16_t(v)); }

namespace dpt {
Result layout(uint16_t mainType, Layout &value) {
    Layout v = {0,0,false};
    switch (mainType) {
    case 1: v = Layout{1,1,false}; break;
    case 2: case 23: v = Layout{1,2,false}; break;
    case 3: v = Layout{1,4,false}; break;
    case 31: v = Layout{1,3,false}; break;
    case 4: case 5: case 6: case 17: case 18: case 20: case 21: case 25: case 26: v.bytes = 1; break;
    case 7: case 8: case 9: case 22: v.bytes = 2; break;
    case 10: case 11: case 30: case 232: v.bytes = 3; break;
    case 12: case 13: case 14: case 15: case 27: v.bytes = 4; break;
    case 16: v.bytes = 14; break;
    case 19: case 29: v.bytes = 8; break;
    case 24: case 28: v.variable = true; break;
    case 251: v.bytes = 6; break;
    default: return Result::Unsupported;
    }
    value = v; return Result::Ok;
}
Result encodeUnsigned(uint64_t value, uint8_t *out, size_t bytes) {
    if (!out) return Result::InvalidArgument;
    if (!bytes || bytes > 8) return Result::InvalidLength;
    if (bytes < 8 && value >= (uint64_t(1) << (bytes * 8))) return Result::OutOfRange;
    for (size_t i = bytes; i; --i) { out[i - 1] = uint8_t(value); value >>= 8; }
    return Result::Ok;
}
Result decodeUnsigned(const uint8_t *data, size_t bytes, uint64_t &value) {
    if (!data) return Result::InvalidArgument;
    if (!bytes || bytes > 8) return Result::InvalidLength;
    uint64_t v = 0;
    for (size_t i = 0; i < bytes; ++i) v = (v << 8) | data[i];
    value = v; return Result::Ok;
}
Result encodeSigned(int64_t value, uint8_t *out, size_t bytes) {
    if (!out) return Result::InvalidArgument;
    if (!bytes || bytes > 8) return Result::InvalidLength;
    if (bytes < 8) {
        int64_t limit = int64_t(1) << (bytes * 8 - 1);
        if (value < -limit || value >= limit) return Result::OutOfRange;
    }
    uint64_t v = uint64_t(value);
    if (bytes < 8) v &= (uint64_t(1) << (bytes * 8)) - 1;
    return encodeUnsigned(v, out, bytes);
}
Result decodeSigned(const uint8_t *data, size_t bytes, int64_t &value) {
    uint64_t v;
    Result r = decodeUnsigned(data, bytes, v);
    if (r != Result::Ok) return r;
    if (data[0] & 0x80) {
        if (bytes < 8) v |= UINT64_MAX << (bytes * 8);
        value = -1 - int64_t(~v);
    } else value = int64_t(v);
    return Result::Ok;
}
Result encodeCompact(uint8_t value, uint8_t bits, uint8_t *out) {
    if (!out || !bits || bits > 6) return Result::InvalidArgument;
    if (value >= (1u << bits)) return Result::OutOfRange;
    *out = value; return Result::Ok;
}
Result decodeCompact(const uint8_t *data, size_t len, uint8_t bits, uint8_t &value) {
    if (!data || !bits || bits > 6) return Result::InvalidArgument;
    if (len != 1) return Result::InvalidLength;
    if (*data >= (1u << bits)) return Result::InvalidValue;
    value = *data; return Result::Ok;
}
Result encodeFloat16(float value, uint8_t *out, size_t capacity) {
    if (!out) return Result::InvalidArgument;
    if (capacity < 2) return Result::InvalidLength;
    if (!isfinite(value)) return Result::InvalidValue;
    if (value < -671088.64 || value > 670760.96) return Result::OutOfRange;
    double scaled = double(value) * 100.0;
    unsigned e = 0;
    double m = round(scaled);
    while ((m < -2048 || m > 2047) && e < 15) { ++e; m = round(scaled / (1u << e)); }
    if (m < -2048 || m > 2047) return Result::OutOfRange;
    int32_t mantissa = int32_t(m);
    uint16_t raw = uint16_t((mantissa < 0 ? 0x8000 : 0) | (e << 11) | (uint32_t(mantissa) & 0x7ff));
    // 0x7fff is reserved as invalid data by the DPT specification.
    if (raw == 0x7fff) return Result::InvalidValue;
    write16(out, raw); return Result::Ok;
}
Result decodeFloat16(const uint8_t *data, size_t len, float &value) {
    if (!data) return Result::InvalidArgument;
    if (len != 2) return Result::InvalidLength;
    uint16_t raw = read16(data);
    if (raw == 0x7fff) return Result::InvalidValue;
    int32_t m = raw & 0x7ff;
    if (raw & 0x8000) m -= 2048;
    value = float(0.01 * m * (1u << ((raw >> 11) & 15)));
    return Result::Ok;
}
Result encodeFloat32(float value, uint8_t *out, size_t capacity) {
    static_assert(sizeof(float) == 4, "KNX requires IEEE-754 binary32");
    if (!out) return Result::InvalidArgument;
    if (capacity < 4) return Result::InvalidLength;
    uint32_t bits; memcpy(&bits, &value, 4); write32(out, bits); return Result::Ok;
}
Result decodeFloat32(const uint8_t *data, size_t len, float &value) {
    if (!data) return Result::InvalidArgument;
    if (len != 4) return Result::InvalidLength;
    uint32_t bits = read32(data); memcpy(&value, &bits, 4); return Result::Ok;
}
Result encodeScaled(double value, double fullScale, uint8_t *out) {
    if (!out || !isfinite(fullScale) || fullScale <= 0) return Result::InvalidArgument;
    if (!isfinite(value)) return Result::InvalidValue;
    if (value < 0 || value > fullScale) return Result::OutOfRange;
    *out = uint8_t(round(value * 255.0 / fullScale)); return Result::Ok;
}
Result decodeScaled(const uint8_t *data, size_t len, double fullScale, double &value) {
    if (!data || !isfinite(fullScale) || fullScale <= 0) return Result::InvalidArgument;
    if (len != 1) return Result::InvalidLength;
    value = *data * fullScale / 255.0; return Result::Ok;
}
static bool validDate(const Date &d) {
    if (!d.month || d.month > 12 || !d.day) return false;
    static const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    bool leap = d.year % 4 == 0 && (d.year % 100 != 0 || d.year % 400 == 0);
    return d.day <= days[d.month - 1] + (d.month == 2 && leap);
}
Result encodeTime(const Time &v, uint8_t *out, size_t capacity) {
    if (!out) return Result::InvalidArgument;
    if (capacity < 3) return Result::InvalidLength;
    if (v.weekday > 7 || v.hour > 23 || v.minute > 59 || v.second > 59) return Result::OutOfRange;
    out[0] = uint8_t((v.weekday << 5) | v.hour); out[1] = v.minute; out[2] = v.second;
    return Result::Ok;
}
Result decodeTime(const uint8_t *data, size_t len, Time &value) {
    if (!data) return Result::InvalidArgument;
    if (len != 3) return Result::InvalidLength;
    Time v = {uint8_t(data[0] >> 5), uint8_t(data[0] & 31), data[1], data[2]};
    uint8_t b[3]; Result r = encodeTime(v, b, 3);
    if (r == Result::Ok) value = v;
    return r;
}
Result encodeDate(const Date &v, uint8_t *out, size_t capacity) {
    if (!out) return Result::InvalidArgument;
    if (capacity < 3) return Result::InvalidLength;
    if (v.year < 1990 || v.year > 2089 || !validDate(v)) return Result::OutOfRange;
    out[0] = v.day; out[1] = v.month; out[2] = uint8_t(v.year % 100); return Result::Ok;
}
Result decodeDate(const uint8_t *data, size_t len, Date &value) {
    if (!data) return Result::InvalidArgument;
    if (len != 3) return Result::InvalidLength;
    if (data[2] > 99) return Result::InvalidValue;
    Date v = {uint16_t((data[2] >= 90 ? 1900 : 2000) + data[2]), data[1], data[0]};
    if (!validDate(v)) return Result::InvalidValue;
    value = v; return Result::Ok;
}
static bool validDateTime(const DateTime &v) {
    if (v.quality & 0x3f) return false;
    if (!(v.flags & 0x10) && (v.date.year < 1900 || v.date.year > 2155)) return false;
    if (!(v.flags & 0x08)) {
        Date d = v.date;
        if (v.flags & 0x10) d.year = 2000; // unknown year allows February 29
        if (!validDate(d)) return false;
    }
    if (!(v.flags & 0x04) && v.time.weekday > 7) return false;
    if (!(v.flags & 0x02) && (v.time.hour > 24 || v.time.minute > 59 || v.time.second > 59 ||
        (v.time.hour == 24 && (v.time.minute || v.time.second)))) return false;
    return true;
}
Result encodeDateTime(const DateTime &v, uint8_t *out, size_t capacity) {
    if (!out) return Result::InvalidArgument;
    if (capacity < 8) return Result::InvalidLength;
    if (!validDateTime(v)) return Result::OutOfRange;
    out[0] = (v.flags & 0x10) ? 0 : uint8_t(v.date.year - 1900);
    out[1] = (v.flags & 8) ? 0 : v.date.month; out[2] = (v.flags & 8) ? 0 : v.date.day;
    out[3] = uint8_t(((v.flags & 4) ? 0 : v.time.weekday << 5) | ((v.flags & 2) ? 0 : v.time.hour));
    out[4] = (v.flags & 2) ? 0 : v.time.minute; out[5] = (v.flags & 2) ? 0 : v.time.second;
    out[6] = v.flags; out[7] = v.quality; return Result::Ok;
}
Result decodeDateTime(const uint8_t *data, size_t len, DateTime &value) {
    if (!data) return Result::InvalidArgument;
    if (len != 8) return Result::InvalidLength;
    if ((data[1] & 0xf0) || (data[2] & 0xe0) || (data[4] & 0xc0) || (data[5] & 0xc0)) return Result::InvalidValue;
    DateTime v = {{uint16_t(1900 + data[0]), data[1], data[2]},
                  {uint8_t(data[3] >> 5), uint8_t(data[3] & 31), data[4], data[5]}, data[6], data[7]};
    if (!validDateTime(v)) return Result::InvalidValue;
    value = v; return Result::Ok;
}
Result encodeScene(uint8_t scene, bool learn, uint8_t *out) {
    if (!out) return Result::InvalidArgument;
    if (scene > 63) return Result::OutOfRange;
    *out = uint8_t(scene | (learn ? 0x80 : 0)); return Result::Ok;
}
Result decodeScene(const uint8_t *data, size_t len, uint8_t &scene, bool &learn) {
    if (!data) return Result::InvalidArgument;
    if (len != 1) return Result::InvalidLength;
    if (*data & 0x40) return Result::InvalidValue;
    scene = *data & 63; learn = (*data & 0x80) != 0; return Result::Ok;
}
Result encodeSceneInfo(uint8_t scene, bool inactive, uint8_t *out) {
    if (!out) return Result::InvalidArgument;
    if (scene > 63) return Result::OutOfRange;
    *out = uint8_t(scene | (inactive ? 0x40 : 0)); return Result::Ok;
}
Result decodeSceneInfo(const uint8_t *data, size_t len, uint8_t &scene, bool &inactive) {
    if (!data) return Result::InvalidArgument;
    if (len != 1) return Result::InvalidLength;
    if (*data & 0x80) return Result::InvalidValue;
    scene = *data & 63; inactive = (*data & 0x40) != 0; return Result::Ok;
}
Result encodeStatusMode(uint8_t flags, uint8_t mode, uint8_t *out) {
    if (!out) return Result::InvalidArgument;
    if (flags > 31 || mode > 2) return Result::OutOfRange;
    *out = uint8_t((flags << 3) | (1u << mode)); return Result::Ok;
}
Result decodeStatusMode(const uint8_t *data, size_t len, uint8_t &flags, uint8_t &mode) {
    if (!data) return Result::InvalidArgument;
    if (len != 1) return Result::InvalidLength;
    uint8_t m = *data & 7;
    if (m != 1 && m != 2 && m != 4) return Result::InvalidValue;
    flags = *data >> 3; mode = m == 1 ? 0 : m == 2 ? 1 : 2; return Result::Ok;
}
Result encodeAccess(const Access &value, uint8_t *out, size_t capacity) {
    if (!out) return Result::InvalidArgument;
    if (capacity < 4) return Result::InvalidLength;
    if (value.code > 999999 || value.flags > 15 || value.index > 15) return Result::OutOfRange;
    uint32_t code = value.code;
    for (int i = 2; i >= 0; --i) { unsigned low = code % 10; code /= 10; out[i] = uint8_t(((code % 10) << 4) | low); code /= 10; }
    out[3] = uint8_t((value.flags << 4) | value.index); return Result::Ok;
}
Result decodeAccess(const uint8_t *data, size_t len, Access &value) {
    if (!data) return Result::InvalidArgument;
    if (len != 4) return Result::InvalidLength;
    uint32_t code = 0;
    for (size_t i = 0; i < 3; ++i) {
        if ((data[i] >> 4) > 9 || (data[i] & 15) > 9) return Result::InvalidValue;
        code = code * 100 + (data[i] >> 4) * 10 + (data[i] & 15);
    }
    value = Access{code, uint8_t(data[3] >> 4), uint8_t(data[3] & 15)}; return Result::Ok;
}
Result encodeString14(const char *value, uint8_t *out, size_t capacity, bool ascii) {
    if (!value || !out) return Result::InvalidArgument;
    if (capacity < 14) return Result::InvalidLength;
    size_t n = 0;
    while (n < 14 && value[n]) { if (ascii && uint8_t(value[n]) > 127) return Result::InvalidValue; ++n; }
    if (n == 14 && value[n]) return Result::OutOfRange;
    memset(out, 0, 14); memcpy(out, value, n); return Result::Ok;
}
Result decodeString14(const uint8_t *data, size_t len, char *out, size_t capacity, bool ascii) {
    if (!data || !out) return Result::InvalidArgument;
    if (len != 14 || capacity < 15) return Result::InvalidLength;
    if (ascii) for (size_t i = 0; i < 14; ++i) if (data[i] > 127) return Result::InvalidValue;
    memcpy(out, data, 14); out[14] = 0; return Result::Ok;
}
static bool validUTF8(const uint8_t *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        uint32_t cp = s[i++]; unsigned extra; uint32_t minimum;
        if (cp < 0x80) continue;
        if (cp >= 0xc2 && cp <= 0xdf) { extra = 1; minimum = 0x80; cp &= 31; }
        else if (cp >= 0xe0 && cp <= 0xef) { extra = 2; minimum = 0x800; cp &= 15; }
        else if (cp >= 0xf0 && cp <= 0xf4) { extra = 3; minimum = 0x10000; cp &= 7; }
        else return false;
        if (extra > n - i) return false;
        while (extra--) { if ((s[i] & 0xc0) != 0x80) return false; cp = (cp << 6) | (s[i++] & 63); }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
    }
    return true;
}
Result encodeString(const char *value, size_t size, uint8_t *out, size_t capacity, size_t &written, bool utf8) {
    written = 0;
    if (!value || !out) return Result::InvalidArgument;
    if (size >= capacity) return Result::InvalidLength;
    if (memchr(value, 0, size) || (utf8 && !validUTF8(reinterpret_cast<const uint8_t *>(value), size))) return Result::InvalidValue;
    memcpy(out, value, size); out[size] = 0; written = size + 1; return Result::Ok;
}
Result decodeString(const uint8_t *data, size_t len, char *out, size_t capacity, bool utf8) {
    if (!data || !out) return Result::InvalidArgument;
    if (!len || capacity < len) return Result::InvalidLength;
    if (data[len - 1] || memchr(data, 0, len - 1) || (utf8 && !validUTF8(data, len - 1))) return Result::InvalidValue;
    memcpy(out, data, len); return Result::Ok;
}
Result encodeRGB(const RGB &v, uint8_t *out, size_t capacity) {
    if (!out) return Result::InvalidArgument;
    if (capacity < 3) return Result::InvalidLength;
    out[0] = v.red; out[1] = v.green; out[2] = v.blue; return Result::Ok;
}
Result decodeRGB(const uint8_t *data, size_t len, RGB &v) {
    if (!data) return Result::InvalidArgument;
    if (len != 3) return Result::InvalidLength;
    v = RGB{data[0], data[1], data[2]}; return Result::Ok;
}
Result encodeRGBW(const RGBW &v, uint8_t *out, size_t capacity) {
    if (!out) return Result::InvalidArgument;
    if (capacity < 6) return Result::InvalidLength;
    if (v.valid > 15) return Result::OutOfRange;
    out[0] = v.red; out[1] = v.green; out[2] = v.blue; out[3] = v.white; out[4] = 0; out[5] = v.valid; return Result::Ok;
}
Result decodeRGBW(const uint8_t *data, size_t len, RGBW &v) {
    if (!data) return Result::InvalidArgument;
    if (len != 6) return Result::InvalidLength;
    if (data[4] || data[5] > 15) return Result::InvalidValue;
    v = RGBW{data[0], data[1], data[2], data[3], data[5]}; return Result::Ok;
}
Result validate(uint16_t mainType, const uint8_t *data, size_t len) {
    if (!data) return Result::InvalidArgument;
    Layout l; Result r = layout(mainType,l); if (r != Result::Ok) return r;
    if ((!l.variable && len != l.bytes) || (l.variable && (!len || len > 254))) return Result::InvalidLength;
    if (l.compactBits) { uint8_t v; return decodeCompact(data,len,l.compactBits,v); }
    switch (mainType) {
    case 9: { float v; return decodeFloat16(data,len,v); }
    case 10: { Time v; return decodeTime(data,len,v); }
    case 11: { Date v; return decodeDate(data,len,v); }
    case 15: { Access v; return decodeAccess(data,len,v); }
    case 17: return data[0] > 63 ? Result::InvalidValue : Result::Ok;
    case 18: return (data[0] & 0x40) ? Result::InvalidValue : Result::Ok;
    case 19: { DateTime v; return decodeDateTime(data,len,v); }
    case 24: case 28:
        if (data[len-1] || memchr(data,0,len-1) || (mainType == 28 && !validUTF8(data,len-1))) return Result::InvalidValue;
        break;
    case 26: return (data[0] & 0x80) ? Result::InvalidValue : Result::Ok;
    case 251: { RGBW v; return decodeRGBW(data,len,v); }
    default: break; // subtype-specific enum, range and reserved bits are not main-type properties
    }
    return Result::Ok;
}
} // namespace dpt
} // namespace knxip
