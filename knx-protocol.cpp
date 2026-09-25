#include "knx-protocol.h"
#include <string.h>

namespace knxip {
void packetHeader(uint8_t *out, uint16_t service, size_t length) {
    out[0] = 6; out[1] = 0x10; write16(out + 2, service); write16(out + 4, uint16_t(length));
}
Result parsePacket(const uint8_t *data, size_t len, PacketView &packet) {
    if (!data) return Result::InvalidArgument;
    if (len < 6 || len > MaxDatagram) return Result::InvalidLength;
    if (data[0] != 6 || data[1] != 0x10) return Result::Unsupported;
    if (read16(data + 4) != len) return Result::InvalidLength;
    packet = PacketView{read16(data + 2), data + 6, len - 6}; return Result::Ok;
}
Result parseCemi(const uint8_t *data, size_t len, CemiView &frame) {
    if (!data) return Result::InvalidArgument;
    if (len < 2) return Result::InvalidLength;
    if (data[0] != 0x11 && data[0] != 0x29 && data[0] != 0x2e) return Result::Unsupported;
    size_t offset = 2 + size_t(data[1]);
    if (offset > len || len - offset < 9) return Result::InvalidLength;
    for (size_t i = 2; i < offset;) {
        if (offset - i < 2 || size_t(data[i + 1]) > offset - i - 2) return Result::InvalidLength;
        i += 2 + size_t(data[i + 1]);
    }
    const uint8_t *p = data + offset;
    size_t npdu = p[6];
    if (!npdu || len - offset != 8 + npdu) return Result::InvalidLength;
    if ((p[0] & 0x80) && npdu > 15) return Result::InvalidLength;
    if ((p[0] & 0x40) || (p[1] & 0x0f)) return Result::Unsupported;
    // Application client supports unnumbered GroupValue read/response/write.
    if (p[7] & 0xfc) return Result::Unsupported;
    uint8_t command = uint8_t(((p[7] & 3) << 2) | (p[8] >> 6));
    if (command > 2) return Result::Unsupported;
    if (command == 0 && (npdu != 1 || (p[8] & 63))) return Result::InvalidValue;
    if (npdu > 1 && (p[8] & 63)) return Result::InvalidValue;
    frame = CemiView{data[0], command, uint8_t(p[8] & 63), uint8_t((p[1] >> 4) & 7),
                     read16(p + 2), read16(p + 4), p + 9, npdu - 1,
                     bool(p[1] & 0x80), bool(p[0] & 1)};
    return Result::Ok;
}
Result encodeCemi(uint8_t code, uint16_t source, uint16_t destination, uint8_t command,
                  const uint8_t *payload, size_t size, bool compact,
                  uint8_t *out, size_t capacity, size_t &written) {
    written = 0;
    if (!out || (size && !payload)) return Result::InvalidArgument;
    if ((code != 0x11 && code != 0x29) || command > 2) return Result::Unsupported;
    if (!destination) return Result::OutOfRange;
    if (command == 0) { size = 0; compact = true; }
    else if (!size || size > MaxPayload || (compact && size != 1)) return Result::InvalidLength;
    if (compact && size && *payload > 63) return Result::OutOfRange;
    size_t n = compact ? 0 : size;
    if (capacity < 11 + n) return Result::InvalidLength;
    out[0] = code; out[1] = 0;
    out[2] = uint8_t((n + 1 <= 15 ? 0xbc : 0x3c) | (code == 0x11 ? 2 : 0));
    out[3] = 0xe0; // group, hop count 6, standard extended-frame format
    write16(out + 4, source); write16(out + 6, destination);
    out[8] = uint8_t(n + 1); out[9] = 0;
    out[10] = uint8_t((command << 6) | (compact && size ? *payload : 0));
    if (n) memcpy(out + 11, payload, n);
    written = 11 + n; return Result::Ok;
}
bool Endpoint::operator==(const Endpoint &other) const { return port == other.port && memcmp(ip, other.ip, 4) == 0; }
void encodeHpai(uint8_t *out, const Endpoint &ep) { out[0] = 8; out[1] = 1; memcpy(out + 2, ep.ip, 4); write16(out + 6, ep.port); }
Result parseHpai(const uint8_t *data, size_t len, Endpoint &ep) {
    if (!data) return Result::InvalidArgument;
    if (len < 8 || data[0] != 8) return Result::InvalidLength;
    if (data[1] != 1) return Result::Unsupported;
    memcpy(ep.ip, data + 2, 4); ep.port = read16(data + 6); return Result::Ok;
}
static Endpoint resolved(Endpoint ep, const Endpoint &sender) {
    if (!(ep.ip[0] | ep.ip[1] | ep.ip[2] | ep.ip[3])) memcpy(ep.ip, sender.ip, 4);
    if (!ep.port) ep.port = sender.port;
    return ep;
}
Result parseDiscovery(const PacketView &p, const Endpoint &sender, DiscoveryView &view) {
    size_t offset = 0; Endpoint control = sender;
    if (p.service == 0x0202) {
        Result r = parseHpai(p.body, p.size, control); if (r != Result::Ok) return r;
        control = resolved(control, sender); offset = 8;
    } else if (p.service != 0x0204) return Result::Unsupported;
    bool device = false, families = false;
    for (size_t i = offset; i < p.size;) {
        if (p.size - i < 2) return Result::InvalidLength;
        size_t n = p.body[i];
        if (n < 2 || n > p.size - i) return Result::InvalidLength;
        uint8_t type = p.body[i + 1];
        if (type == 1) { if (device || n != 54) return Result::InvalidLength; device = true; }
        if (type == 2) { if (families || n < 4 || (n & 1)) return Result::InvalidLength; families = true; }
        i += n;
    }
    if (!device || !families) return Result::InvalidValue;
    view = DiscoveryView{control, p.body + offset, p.size - offset}; return Result::Ok;
}
const uint8_t *findDib(const DiscoveryView &view, uint8_t type, size_t &size) {
    size = 0;
    for (size_t i = 0; i + 2 <= view.size;) {
        size_t n = view.dibs[i];
        if (n < 2 || n > view.size - i) return 0;
        if (view.dibs[i + 1] == type) { size = n; return view.dibs + i; }
        i += n;
    }
    return 0;
}

Tunnel::Tunnel() : transmit_(0), context_(0) { reset(); }
void Tunnel::reset() {
    state_ = State::Disconnected; channel_ = txSequence_ = rxSequence_ = retries_ = heartRetries_ = status_ = 0;
    address_ = 0; pending_ = heartbeat_ = received_ = false;
    stateSince_ = sentAt_ = heartAt_ = lastHeart_ = 0; pendingSize_ = 0; result_ = Result::Ok;
}
bool Tunnel::emit(const Endpoint &ep, const uint8_t *data, size_t len) {
    if (!transmit_ || !transmit_(ep, data, len, context_)) { result_ = Result::IoError; return false; }
    return true;
}
Result Tunnel::connect(const Endpoint &local, const Endpoint &server, uint32_t now,
                       Transmit transmit, void *context, bool nat) {
    if (state_ != State::Disconnected) return Result::Busy;
    if (!transmit || !local.port || !server.port || !(server.ip[0] | server.ip[1] | server.ip[2] | server.ip[3])) return Result::InvalidArgument;
    reset(); transmit_ = transmit; context_ = context; local_ = local; server_ = server;
    if (nat) local_ = Endpoint{{0,0,0,0},0};
    uint8_t packet[26]; packetHeader(packet, 0x0205, sizeof(packet));
    encodeHpai(packet + 6, local_); encodeHpai(packet + 14, local_);
    packet[22] = 4; packet[23] = 4; packet[24] = 2; packet[25] = 0;
    if (!emit(server_, packet, sizeof(packet))) return result_;
    state_ = State::Connecting; stateSince_ = now; return Result::Ok;
}
void Tunnel::control(uint16_t service) {
    uint8_t packet[16]; packetHeader(packet, service, sizeof(packet));
    packet[6] = channel_; packet[7] = 0; encodeHpai(packet + 8, local_);
    emit(server_, packet, sizeof(packet));
}
Result Tunnel::disconnect(uint32_t now) {
    if (state_ == State::Disconnected) return Result::NotConnected;
    if (state_ == State::Connecting) { reset(); return Result::Ok; }
    if (state_ == State::Disconnecting) return Result::Busy;
    state_ = State::Disconnecting; stateSince_ = now; pending_ = heartbeat_ = false;
    control(0x0209); return result_ == Result::IoError ? result_ : Result::Ok;
}
Result Tunnel::send(const uint8_t *cemi, size_t len, uint32_t now) {
    if (state_ != State::Connected) return Result::NotConnected;
    if (pending_) return Result::Busy;
    if (!cemi) return Result::InvalidArgument;
    if (len > MaxCemi) return Result::InvalidLength;
    CemiView frame; Result r = parseCemi(cemi, len, frame);
    if (r != Result::Ok) return r;
    if (frame.code != 0x11 || !frame.group) return Result::Unsupported;
    packetHeader(pendingData_, 0x0420, len + 10);
    pendingData_[6] = 4; pendingData_[7] = channel_; pendingData_[8] = txSequence_; pendingData_[9] = 0;
    memcpy(pendingData_ + 10, cemi, len); pendingSize_ = len + 10;
    if (!emit(data_, pendingData_, pendingSize_)) return result_;
    pending_ = true; retries_ = 0; sentAt_ = now; result_ = Result::Ok; return Result::Ok;
}
void Tunnel::retry(uint32_t now) {
    if (retries_++ == 0) { emit(data_, pendingData_, pendingSize_); sentAt_ = now; }
    else { result_ = Result::NotConnected; disconnect(now); }
}
void Tunnel::tick(uint32_t now) {
    if (state_ == State::Connecting && uint32_t(now - stateSince_) >= 10000) { state_ = State::Disconnected; result_ = Result::NotConnected; }
    if (state_ == State::Disconnecting && uint32_t(now - stateSince_) >= 5000) { state_ = State::Disconnected; address_ = 0; }
    if (state_ != State::Connected) return;
    if (pending_ && uint32_t(now - sentAt_) >= 1000) retry(now);
    if (state_ != State::Connected) return;
    if (heartbeat_ && uint32_t(now - heartAt_) >= 10000) {
        if (++heartRetries_ >= 3) { result_ = Result::NotConnected; disconnect(now); }
        else { control(0x0207); heartAt_ = now; }
    } else if (!heartbeat_ && uint32_t(now - lastHeart_) >= 60000) {
        control(0x0207); heartbeat_ = true; heartRetries_ = 0; heartAt_ = now;
    }
}
Result Tunnel::receive(const uint8_t *data, size_t len, const Endpoint &sender,
                       uint32_t now, CemiView &frame, bool &deliver) {
    deliver = false;
    PacketView p; Result r = parsePacket(data, len, p); if (r != Result::Ok) return r;
    const uint8_t *b = p.body;
    if (p.service == 0x0206) {
        if (state_ != State::Connecting || sender != server_) return Result::Unsupported;
        if (p.size < 2) return Result::InvalidLength;
        if (b[1]) {
            if (p.size != 2) return Result::InvalidLength;
            status_ = b[1]; state_ = State::Disconnected; result_ = Result::NotConnected; return result_;
        }
        if (p.size != 14 || b[10] != 4 || b[11] != 4) return Result::InvalidLength;
        Endpoint ep; r = parseHpai(b + 2, 8, ep); if (r != Result::Ok) return r;
        ep = resolved(ep, sender);
        if (ep.ip[0] >= 224 || !ep.port || !read16(b + 12)) return Result::InvalidValue;
        data_ = ep; channel_ = b[0]; address_ = read16(b + 12);
        state_ = State::Connected; lastHeart_ = now; result_ = Result::Ok; return Result::Ok;
    }
    if (state_ != State::Connected && state_ != State::Disconnecting) return Result::NotConnected;
    if (p.service == 0x0421) {
        if (state_ != State::Connected || sender != data_) return Result::Unsupported;
        if (p.size != 4 || b[0] != 4) return Result::InvalidLength;
        if (b[1] != channel_ || !pending_ || b[2] != txSequence_) return Result::Unsupported;
        status_ = b[3];
        if (status_) { retry(now); return Result::InvalidValue; }
        pending_ = false; ++txSequence_; result_ = Result::Ok; return Result::Ok;
    }
    if (p.service == 0x0420) {
        if (state_ != State::Connected || sender != data_) return Result::Unsupported;
        if (p.size < 6 || b[0] != 4 || b[3]) return Result::InvalidLength;
        if (b[1] != channel_) return Result::Unsupported;
        if (b[4] != 0x29 && b[4] != 0x2e) return Result::Unsupported;
        bool duplicate = received_ && b[2] == uint8_t(rxSequence_ - 1);
        if (b[2] != rxSequence_ && !duplicate) return Result::InvalidValue;
        // Validate cEMI even for duplicates before acknowledging it.
        r = parseCemi(b + 4, p.size - 4, frame);
        // Valid but unsupported application services still consume a tunnel sequence.
        // Structural truncation or invalid GroupValue frames do not.
        if (r != Result::Ok && r != Result::Unsupported) return r;
        uint8_t ack[10]; packetHeader(ack, 0x0421, 10);
        ack[6] = 4; ack[7] = channel_; ack[8] = b[2]; ack[9] = 0;
        emit(data_, ack, sizeof(ack));
        if (!duplicate) { ++rxSequence_; received_ = true; deliver = r == Result::Ok && frame.code == 0x29 && frame.group; }
        return Result::Ok;
    }
    if (sender != server_) return Result::Unsupported;
    if (p.service == 0x0208) {
        if (p.size != 2) return Result::InvalidLength;
        if (state_ != State::Connected || !heartbeat_ || b[0] != channel_) return Result::Unsupported;
        status_ = b[1];
        if (status_) { result_ = Result::NotConnected; disconnect(now); return result_; }
        heartbeat_ = false; lastHeart_ = now; return Result::Ok;
    }
    if (p.service == 0x0209) {
        if (p.size != 10 || b[1]) return Result::InvalidLength;
        Endpoint ep; r = parseHpai(b + 2, 8, ep); if (r != Result::Ok) return r;
        uint8_t response[8]; packetHeader(response, 0x020a, 8); response[6] = b[0]; response[7] = b[0] == channel_ ? 0 : 0x21;
        emit(server_, response, 8);
        if (b[0] == channel_) { state_ = State::Disconnected; pending_ = heartbeat_ = false; address_ = 0; }
        return Result::Ok;
    }
    if (p.service == 0x020a) {
        if (p.size != 2) return Result::InvalidLength;
        if (state_ != State::Disconnecting || b[0] != channel_) return Result::Unsupported;
        status_ = b[1]; state_ = State::Disconnected; address_ = 0; return Result::Ok;
    }
    return Result::Unsupported;
}
} // namespace knxip
