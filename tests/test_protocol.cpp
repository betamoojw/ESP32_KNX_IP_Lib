#include "knx-codec.h"
#include "knx-protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
using namespace knxip;
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define OK(x) CHECK((x) == Result::Ok)

static void codecs() {
    uint8_t b[256] = {}; uint64_t u; int64_t s; float f; double scaled;
    for (unsigned bits = 1; bits <= 6; ++bits) {
        uint8_t v;
        OK(dpt::encodeCompact(uint8_t((1u << bits) - 1), bits, b));
        OK(dpt::decodeCompact(b, 1, bits, v)); CHECK(v == (1u << bits) - 1);
        CHECK(dpt::encodeCompact(uint8_t(1u << bits), bits, b) == Result::OutOfRange);
    }
    for (size_t n = 1; n <= 8; ++n) {
        uint64_t max = n == 8 ? UINT64_MAX : (uint64_t(1) << (8*n)) - 1;
        OK(dpt::encodeUnsigned(max, b, n)); OK(dpt::decodeUnsigned(b, n, u)); CHECK(u == max);
        int64_t lo = n == 8 ? INT64_MIN : -(int64_t(1) << (8*n-1));
        int64_t hi = n == 8 ? INT64_MAX : (int64_t(1) << (8*n-1))-1;
        OK(dpt::encodeSigned(lo, b, n)); OK(dpt::decodeSigned(b, n, s)); CHECK(s == lo);
        OK(dpt::encodeSigned(hi, b, n)); OK(dpt::decodeSigned(b, n, s)); CHECK(s == hi);
        if (n < 8) CHECK(dpt::encodeSigned(hi + 1, b, n) == Result::OutOfRange);
    }
    OK(dpt::encodeUnsigned(0x12345678, b, 4)); CHECK(b[0] == 0x12 && b[3] == 0x78);
    CHECK(dpt::encodeUnsigned(0, b, 0) == Result::InvalidLength);
    CHECK(dpt::decodeUnsigned(nullptr, 1, u) == Result::InvalidArgument);
    OK(dpt::encodeFloat16(-20.48f, b, 2)); CHECK(read16(b) == 0x8000);
    OK(dpt::decodeFloat16(b, 2, f)); CHECK(fabs(f + 20.48f) < .0001f);
    OK(dpt::encodeFloat16(-.01f, b, 2)); CHECK(read16(b) == 0x87ff);
    const uint8_t temp[] = {0x0c,0x1a}; OK(dpt::decodeFloat16(temp, 2, f)); CHECK(fabs(f - 21.0f) < .0001f);
    CHECK(dpt::encodeFloat16(INFINITY, b, 2) == Result::InvalidValue);
    CHECK(dpt::encodeFloat16(NAN, b, 2) == Result::InvalidValue);
    CHECK(dpt::encodeFloat16(1e8f, b, 2) == Result::OutOfRange);
    write16(b, 0x7fff); CHECK(dpt::decodeFloat16(b, 2, f) == Result::InvalidValue);
    // Exhaustive raw DPT9 values: signed mantissa/exponent oracle, including negative endpoints.
    for (unsigned raw = 0; raw <= 65535; ++raw) {
        if (raw == 0x7fff) continue;
        write16(b, uint16_t(raw)); OK(dpt::decodeFloat16(b, 2, f));
        int m = int(raw & 2047) - ((raw & 32768) ? 2048 : 0);
        double expected = .01 * m * (1u << ((raw >> 11) & 15));
        CHECK(fabs(double(f) - expected) <= fabs(expected)*1e-7 + 1e-5);
        OK(dpt::encodeFloat16(f, b, 2)); float decoded;
        OK(dpt::decodeFloat16(b, 2, decoded)); CHECK(f == decoded);
    }
    OK(dpt::encodeFloat32(-12.5f, b, 4)); CHECK(read32(b) == 0xc1480000);
    OK(dpt::decodeFloat32(b, 4, f)); CHECK(f == -12.5f);
    write32(b, 0x80000000); OK(dpt::decodeFloat32(b, 4, f)); CHECK(signbit(f));
    write32(b, 0x7fc00000); OK(dpt::decodeFloat32(b, 4, f)); CHECK(isnan(f));
    OK(dpt::encodeScaled(50, 100, b)); CHECK(b[0] == 128);
    OK(dpt::decodeScaled(b, 1, 100, scaled)); CHECK(fabs(scaled - 50.1960784314) < 1e-9);
    OK(dpt::encodeScaled(360, 360, b)); CHECK(b[0] == 255);
    CHECK(dpt::encodeScaled(-1, 100, b) == Result::OutOfRange);
    dpt::Time t = {7,23,59,59}, t2;
    OK(dpt::encodeTime(t, b, 3)); CHECK(b[0] == 0xf7);
    OK(dpt::decodeTime(b, 3, t2)); CHECK(t2.weekday == 7 && t2.hour == 23);
    b[1] = 0x80; CHECK(dpt::decodeTime(b, 3, t2) != Result::Ok);
    dpt::Date date = {2000,2,29}, d2;
    OK(dpt::encodeDate(date, b, 3)); CHECK(b[0] == 29 && b[1] == 2 && b[2] == 0);
    OK(dpt::decodeDate(b, 3, d2)); CHECK(d2.year == 2000);
    date.year = 2001; CHECK(dpt::encodeDate(date, b, 3) == Result::OutOfRange);
    date = dpt::Date{1990,1,1}; OK(dpt::encodeDate(date,b,3)); CHECK(b[2] == 90);
    OK(dpt::decodeDate(b,3,d2)); CHECK(d2.year == 1990);
    date = dpt::Date{2089,12,31}; OK(dpt::encodeDate(date,b,3)); CHECK(b[2] == 89);
    dpt::DateTime dt = {{2026,9,25},{5,24,0,0},0,0xc0}, dt2;
    OK(dpt::encodeDateTime(dt,b,8)); CHECK(b[0] == 126 && b[3] == 0xb8);
    OK(dpt::decodeDateTime(b,8,dt2)); CHECK(dt2.time.hour == 24 && dt2.quality == 0xc0);
    dt.time.minute = 1; CHECK(dpt::encodeDateTime(dt,b,8) == Result::OutOfRange);
    dt = dpt::DateTime{{0,0,0},{0,0,0,0},0x3e,0}; OK(dpt::encodeDateTime(dt,b,8)); OK(dpt::decodeDateTime(b,8,dt2));
    uint8_t scene, mode, flags; bool flag;
    OK(dpt::encodeScene(63,true,b)); CHECK(b[0] == 0xbf);
    OK(dpt::decodeScene(b,1,scene,flag)); CHECK(scene == 63 && flag);
    b[0] = 0x40; CHECK(dpt::decodeScene(b,1,scene,flag) == Result::InvalidValue);
    OK(dpt::encodeSceneInfo(63,true,b)); CHECK(b[0] == 0x7f);
    OK(dpt::decodeSceneInfo(b,1,scene,flag)); CHECK(scene == 63 && flag);
    OK(dpt::encodeStatusMode(31,2,b)); CHECK(b[0] == 0xfc);
    OK(dpt::decodeStatusMode(b,1,flags,mode)); CHECK(flags == 31 && mode == 2);
    b[0] = 0xff; CHECK(dpt::decodeStatusMode(b,1,flags,mode) == Result::InvalidValue);
    dpt::Access access = {123456,4,13}, access2;
    OK(dpt::encodeAccess(access,b,4)); CHECK(read32(b) == 0x1234564d);
    OK(dpt::decodeAccess(b,4,access2)); CHECK(access2.code == 123456 && access2.index == 13);
    b[0] = 0xfa; CHECK(dpt::decodeAccess(b,4,access2) == Result::InvalidValue);
    char text[256]; size_t written;
    OK(dpt::encodeString14("12345678901234",b,14)); OK(dpt::decodeString14(b,14,text,15)); CHECK(strcmp(text,"12345678901234") == 0);
    OK(dpt::encodeString14("KNX",b,14)); CHECK(b[3] == 0 && b[13] == 0);
    CHECK(dpt::encodeString14("123456789012345",b,14) == Result::OutOfRange);
    const char utf[] = "\xe4\xbd\xa0\xe5\xa5\xbd";
    OK(dpt::encodeString(utf,6,b,7,written)); CHECK(written == 7);
    OK(dpt::decodeString(b,7,text,sizeof(text))); CHECK(strcmp(utf,text) == 0);
    const char invalid[] = "\xc0\x80"; CHECK(dpt::encodeString(invalid,2,b,7,written) == Result::InvalidValue);
    const char surrogate[] = "\xed\xa0\x80"; CHECK(dpt::encodeString(surrogate,3,b,7,written) == Result::InvalidValue);
    dpt::RGB rgb = {1,2,3}, rgb2; OK(dpt::encodeRGB(rgb,b,3)); OK(dpt::decodeRGB(b,3,rgb2)); CHECK(rgb2.blue == 3);
    dpt::RGBW rgbw = {1,2,3,4,15}, rgbw2;
    OK(dpt::encodeRGBW(rgbw,b,6)); CHECK(b[4] == 0 && b[5] == 15);
    OK(dpt::decodeRGBW(b,6,rgbw2)); CHECK(rgbw2.white == 4);
    b[4] = 1; CHECK(dpt::decodeRGBW(b,6,rgbw2) == Result::InvalidValue);
    dpt::Layout layout;
    for (unsigned main = 1; main <= 31; ++main) OK(dpt::layout(main,layout));
    OK(dpt::layout(17,layout)); CHECK(layout.compactBits == 0 && layout.bytes == 1);
    CHECK(dpt::layout(999,layout) == Result::Unsupported);
}
static void framing() {
    const uint8_t golden[] = {6,0x10,5,0x30,0,17,0x29,0,0xbc,0xe0,0x11,1,0x0a,3,1,0,0x81};
    uint8_t data[MaxDatagram] = {}; uint8_t bit = 1; size_t written;
    OK(encodeCemi(0x29,0x1101,0x0a03,2,&bit,1,true,data+6,sizeof(data)-6,written));
    packetHeader(data,0x0530,written+6); CHECK(written+6 == sizeof(golden)); CHECK(memcmp(data,golden,sizeof(golden)) == 0);
    PacketView p; CemiView f; OK(parsePacket(data,17,p)); OK(parseCemi(p.body,p.size,f));
    CHECK(f.source == 0x1101 && f.destination == 0x0a03 && f.command == 2 && f.compact == 1 && f.size == 0);
    for (size_t len = 0; len < 17; ++len) CHECK(parsePacket(data,len,p) != Result::Ok);
    for (size_t len = 0; len < 11; ++len) CHECK(parseCemi(data+6,len,f) != Result::Ok);
    for (size_t field = 0; field < 2; ++field) { data[field] ^= 1; CHECK(parsePacket(data,17,p) != Result::Ok); data[field] ^= 1; }
    data[5] = 18; CHECK(parsePacket(data,17,p) == Result::InvalidLength); data[5] = 17;
    data[14] = 255; CHECK(parseCemi(data+6,11,f) == Result::InvalidLength); data[14] = 1;
    // Additional information TLV followed by unchanged cEMI control/addresses.
    uint8_t extra[15] = {0x29,4,1,2,0xaa,0xbb}; memcpy(extra+6,golden+8,9);
    OK(parseCemi(extra,15,f)); CHECK(f.destination == 0x0a03);
    extra[3] = 3; CHECK(parseCemi(extra,15,f) == Result::InvalidLength);
    extra[1] = 255; CHECK(parseCemi(extra,15,f) == Result::InvalidLength);
    uint8_t payload[254]; memset(payload,0xa5,sizeof(payload));
    OK(encodeCemi(0x29,0x1101,0x0a03,2,payload,254,false,data,sizeof(data),written));
    CHECK(written == 265 && data[2] == 0x3c && data[8] == 255);
    OK(parseCemi(data,written,f)); CHECK(f.size == 254 && f.payload[253] == 0xa5);
    data[2] |= 0x80; CHECK(parseCemi(data,written,f) == Result::InvalidLength);
    OK(encodeCemi(0x11,0x1101,0x0a03,0,nullptr,0,true,data,sizeof(data),written));
    CHECK(written == 11 && data[2] == 0xbe && data[10] == 0);
    OK(parseCemi(data,written,f)); CHECK(f.command == 0);
    data[10] = 1; CHECK(parseCemi(data,written,f) == Result::InvalidValue);
    CHECK(encodeCemi(0x29,1,1,2,payload,255,false,data,sizeof(data),written) == Result::InvalidLength);
    // Deterministic malformed-input smoke fuzzing, run with sanitizers where available.
    uint32_t seed = 42;
    for (unsigned i = 0; i < 20000; ++i) {
        size_t len = i % sizeof(data);
        for (size_t j = 0; j < len; ++j) { seed = seed*1664525u+1013904223u; data[j] = uint8_t(seed >> 24); }
        uint8_t *exact = static_cast<uint8_t *>(malloc(len ? len : 1));
        CHECK(exact); memcpy(exact,data,len);
        parsePacket(exact,len,p); parseCemi(exact,len,f);
        free(exact);
    }
}
struct Wire {
    uint8_t last[MaxDatagram]; size_t size; unsigned sends; Endpoint ep; bool fail;
    static bool send(const Endpoint &ep, const uint8_t *data, size_t len, void *ctx) {
        Wire &w = *static_cast<Wire *>(ctx); ++w.sends; w.ep = ep; w.size = len; memcpy(w.last,data,len); return !w.fail;
    }
};
static const Endpoint local = {{192,168,1,10},3672}, server = {{192,168,1,20},3671}, peer = {{192,168,1,20},4000};
static void connected(Tunnel &t, Wire &w, uint32_t now = 0, bool nat = false) {
    OK(t.connect(local,server,now,Wire::send,&w,nat)); CHECK(w.size == 26 && read16(w.last+2) == 0x0205);
    CHECK(w.last[22] == 4 && w.last[24] == 2);
    if (nat) CHECK(read32(w.last+8) == 0 && read16(w.last+12) == 0);
    uint8_t reply[20] = {}; packetHeader(reply,0x0206,sizeof(reply)); reply[6] = 7;
    encodeHpai(reply+8,peer); reply[16] = 4; reply[17] = 4; write16(reply+18,0x110a);
    CemiView f; bool deliver;
    Endpoint wrong = server; ++wrong.port;
    CHECK(t.receive(reply,20,wrong,now,f,deliver) == Result::Unsupported);
    CHECK(t.state() == Tunnel::State::Connecting);
    OK(t.receive(reply,20,server,now,f,deliver)); CHECK(!deliver && t.address() == 0x110a && t.state() == Tunnel::State::Connected);
}
static void tunnelTests() {
    Wire w = {}; Tunnel t; connected(t,w);
    uint8_t cemi[MaxCemi]; size_t n; uint8_t bit = 1;
    OK(encodeCemi(0x11,0x110a,0x0a03,2,&bit,1,true,cemi,sizeof(cemi),n));
    OK(t.send(cemi,n,10)); CHECK(t.pending() && w.ep == peer && w.last[8] == 0 && w.last[10] == 0x11);
    CHECK(t.send(cemi,n,11) == Result::Busy);
    unsigned before = w.sends; t.tick(1009); CHECK(w.sends == before); t.tick(1010); CHECK(w.sends == before+1 && w.last[8] == 0);
    uint8_t ack[] = {6,0x10,4,0x21,0,10,4,7,0,0}; CemiView f; bool deliver;
    OK(t.receive(ack,10,peer,1011,f,deliver)); CHECK(!t.pending());
    // Independent transmit counter including rollover.
    for (unsigned sequence = 1; sequence <= 256; ++sequence) {
        OK(t.send(cemi,n,1020+sequence)); CHECK(w.last[8] == uint8_t(sequence));
        ack[8] = uint8_t(sequence); OK(t.receive(ack,10,peer,1020+sequence,f,deliver));
    }
    uint8_t request[32] = {}; packetHeader(request,0x0420,21); request[6]=4; request[7]=7;
    memcpy(request+10,cemi,n); request[10] = 0x29;
    OK(t.receive(request,21,peer,2000,f,deliver)); CHECK(deliver && f.destination == 0x0a03 && read16(w.last+2) == 0x0421);
    OK(t.receive(request,21,peer,2001,f,deliver)); CHECK(!deliver); // duplicate acknowledged
    request[8] = 3; before = w.sends; CHECK(t.receive(request,21,peer,2002,f,deliver) == Result::InvalidValue); CHECK(w.sends == before);
    request[8] = 1; request[10] = 0x2e; OK(t.receive(request,21,peer,2003,f,deliver)); CHECK(!deliver); // link confirmation ACKed
    request[8] = 2; request[10] = 0x29; request[18] = 255;
    before = w.sends; CHECK(t.receive(request,21,peer,2004,f,deliver) == Result::InvalidLength); CHECK(w.sends == before);
    request[18] = 1; OK(t.receive(request,21,peer,2005,f,deliver)); CHECK(deliver);
    // Every cut is rejected before dispatch, even if the IP total length is rewritten.
    for (size_t len = 6; len < 21; ++len) { write16(request+4,uint16_t(len)); CHECK(t.receive(request,len,peer,2100,f,deliver) != Result::Ok); CHECK(!deliver); }
    t.tick(60000); CHECK(read16(w.last+2) == 0x0207 && w.ep == server);
    uint8_t heart[] = {6,0x10,2,8,0,8,7,0}; OK(t.receive(heart,8,server,60001,f,deliver));
    OK(t.disconnect(61000)); CHECK(t.state() == Tunnel::State::Disconnecting && read16(w.last+2) == 0x0209);
    uint8_t bye[] = {6,0x10,2,10,0,8,7,0}; OK(t.receive(bye,8,server,61001,f,deliver)); CHECK(t.state() == Tunnel::State::Disconnected);
    connected(t,w,100000,true); OK(t.send(cemi,n,100000)); t.tick(101000); t.tick(102000);
    CHECK(t.state() == Tunnel::State::Disconnecting && read16(w.last+2) == 0x0209); t.tick(107000); CHECK(t.state() == Tunnel::State::Disconnected);
    connected(t,w,200000); t.tick(260000); t.tick(270000); t.tick(280000); t.tick(290000);
    CHECK(t.state() == Tunnel::State::Disconnecting); t.tick(295000);
    connected(t,w,UINT32_MAX-100); OK(t.send(cemi,n,UINT32_MAX-100)); before=w.sends; t.tick(900); CHECK(w.sends == before+1);
    t.reset(); OK(t.connect(local,server,0,Wire::send,&w)); t.tick(10000); CHECK(t.state() == Tunnel::State::Disconnected);
    w.fail = true; CHECK(t.connect(local,server,0,Wire::send,&w) == Result::IoError); CHECK(t.state() == Tunnel::State::Disconnected);
    w.fail = false; connected(t,w,1000);
    OK(t.send(cemi,n,1000)); ack[8]=0; ack[9]=0x29;
    before=w.sends; CHECK(t.receive(ack,10,peer,1001,f,deliver) == Result::InvalidValue); CHECK(w.sends == before+1);
    CHECK(t.receive(ack,10,peer,1002,f,deliver) == Result::InvalidValue); CHECK(t.state() == Tunnel::State::Disconnecting);
    t.reset(); connected(t,w,2000);
    uint8_t serverBye[16] = {}; packetHeader(serverBye,0x0209,16);serverBye[6]=7;encodeHpai(serverBye+8,server);
    OK(t.receive(serverBye,16,server,2001,f,deliver)); CHECK(t.state() == Tunnel::State::Disconnected && w.last[7] == 0);
}
static void discovery() {
    uint8_t b[72] = {}; packetHeader(b,0x0202,sizeof(b)); encodeHpai(b+6,server);
    b[14] = 54; b[15] = 1; b[68] = 4; b[69] = 2; b[70] = 4; b[71] = 1;
    PacketView p; DiscoveryView d; OK(parsePacket(b,sizeof(b),p)); OK(parseDiscovery(p,server,d)); CHECK(d.control == server);
    size_t n; const uint8_t *dib = findDib(d,2,n); CHECK(dib && n == 4 && dib[2] == 4);
    CHECK(!findDib(d,255,n));
    for (size_t len = 0; len < sizeof(b); ++len) {
        if (len < 6) continue;
        packetHeader(b,0x0202,len); OK(parsePacket(b,len,p)); CHECK(parseDiscovery(p,server,d) != Result::Ok);
    }
    packetHeader(b,0x0202,sizeof(b)); b[68] = 0; OK(parsePacket(b,sizeof(b),p)); CHECK(parseDiscovery(p,server,d) == Result::InvalidLength);
}
int main() { codecs(); framing(); tunnelTests(); discovery(); printf("PASS: %u checks plus 20000 malformed-input iterations\n",checks); }
