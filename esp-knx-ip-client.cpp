#include "esp-knx-ip.h"

static knxip::Endpoint endpoint(IPAddress ip, uint16_t port) {
    return knxip::Endpoint{{ip[0], ip[1], ip[2], ip[3]}, port};
}
static IPAddress local_address() {
#ifdef ESP32
    NetworkInterface *interface = Network.getDefaultInterface();
    return interface && interface->hasIP() ? interface->localIP() : IPAddress();
#else
    return WiFi.localIP();
#endif
}
void ESPKNXIP::__clear_udp(KnxUDP &socket) {
#ifdef ESP32
    socket.clear();
#else
    socket.flush();
#endif
}
bool ESPKNXIP::__transmit(const knxip::Endpoint &ep, const uint8_t *data, size_t size, void *context) {
    ESPKNXIP *self = static_cast<ESPKNXIP *>(context);
    if (!self->udp.beginPacket(IPAddress(ep.ip[0], ep.ip[1], ep.ip[2], ep.ip[3]), ep.port)) return false;
    size_t written = self->udp.write(data, size);
    int sent = self->udp.endPacket();
    return written == size && sent == 1;
}
knxip::Result ESPKNXIP::start_routing() {
    if (tunnel.state() != knxip::Tunnel::State::Disconnected) return last_result_ = knxip::Result::Busy;
    if (uint32_t(local_address()) == 0) return last_result_ = knxip::Result::NotConnected;
    udp.stop(); routing_ = false;
#ifdef ESP32
    int ok = udp.beginMulticast(MULTICAST_IP, MULTICAST_PORT);
#else
    int ok = udp.beginMulticast(WiFi.localIP(), MULTICAST_IP, MULTICAST_PORT);
#endif
    routing_ = ok != 0; local_port_ = ok ? MULTICAST_PORT : 0;
    routing_sent_ = false; routing_busy_factor_ = 0; routing_wait_ms_ = 0;
    return last_result_ = ok ? knxip::Result::Ok : knxip::Result::IoError;
}
knxip::Result ESPKNXIP::start_tunnel(IPAddress server, uint16_t port, uint16_t local_port, bool nat) {
    if (tunnel.state() != knxip::Tunnel::State::Disconnected) return last_result_ = knxip::Result::Busy;
    if (!port || !local_port || local_port == MULTICAST_PORT || uint32_t(server) == 0 || server[0] >= 224)
        return last_result_ = knxip::Result::InvalidArgument;
    if (discovery_callback_ && local_port == discovery_port_) return last_result_ = knxip::Result::Busy;
    IPAddress local = local_address();
    if (uint32_t(local) == 0) return last_result_ = knxip::Result::NotConnected;
    udp.stop(); routing_ = false;
    if (!udp.begin(local_port)) return last_result_ = knxip::Result::IoError;
    local_port_ = local_port;
    return last_result_ = tunnel.connect(endpoint(local, local_port), endpoint(server, port), millis(), __transmit, this, nat);
}
knxip::Result ESPKNXIP::disconnect_tunnel() { return last_result_ = tunnel.disconnect(millis()); }
void ESPKNXIP::__routing_decay(uint32_t now) {
    if (routing_busy_factor_ && int32_t(now - routing_decay_at_) >= 0) {
        uint32_t steps = uint32_t(now - routing_decay_at_) / 5 + 1;
        routing_busy_factor_ = steps >= routing_busy_factor_ ? 0 : uint16_t(routing_busy_factor_ - steps);
        routing_decay_at_ += steps * 5;
    }
}
knxip::Result ESPKNXIP::send_dpt(address_t const &receiver, knx_command_type_t ct, uint16_t main_type, const uint8_t *payload, size_t size) {
    knxip::dpt::Layout layout;
    knxip::Result r = knxip::dpt::layout(main_type,layout);
    if (r != knxip::Result::Ok) return last_result_ = r;
    if (ct == KNX_CT_READ) return send_payload(receiver,ct,nullptr,0,true);
    r = knxip::dpt::validate(main_type,payload,size);
    if (r != knxip::Result::Ok) return last_result_ = r;
    return send_payload(receiver,ct,payload,size,layout.compactBits != 0);
}
knxip::Result ESPKNXIP::message_payload(const message_t &msg, uint16_t main_type, const uint8_t *&payload, size_t &size) {
    payload = nullptr; size = 0;
    if (msg.ct != KNX_CT_WRITE && msg.ct != KNX_CT_ANSWER) return knxip::Result::Unsupported;
    if (!msg.data || !msg.data_len) return knxip::Result::InvalidLength;
    knxip::dpt::Layout layout;
    knxip::Result r = knxip::dpt::layout(main_type,layout);
    if (r != knxip::Result::Ok) return r;
    const uint8_t *p = layout.compactBits ? msg.data : msg.data+1;
    size_t n = layout.compactBits ? msg.data_len : msg.data_len-1;
    if (!layout.compactBits && msg.data[0]) return knxip::Result::InvalidValue;
    r = knxip::dpt::validate(main_type,p,n);
    if (r == knxip::Result::Ok) { payload = p; size = n; }
    return r;
}
knxip::Result ESPKNXIP::send_payload(address_t const &receiver, knx_command_type_t ct,
                                    const uint8_t *payload, size_t size, bool compact) {
    if (!routing_ && tunnel.state() != knxip::Tunnel::State::Connected) return last_result_ = knxip::Result::NotConnected;
    uint32_t now = millis();
    if (routing_) {
        __routing_decay(now);
        if (uint32_t(now - routing_wait_start_) < routing_wait_ms_ ||
            (routing_sent_ && uint32_t(now - routing_last_send_) < 20)) return last_result_ = knxip::Result::Busy;
    }
    uint8_t packet[knxip::MaxCemi + 6]; size_t written;
    uint16_t src = routing_ ? uint16_t((uint16_t(physaddr.bytes.high) << 8) | physaddr.bytes.low) : tunnel.address();
    uint16_t dst = uint16_t((uint16_t(receiver.bytes.high) << 8) | receiver.bytes.low);
    knxip::Result r = knxip::encodeCemi(routing_ ? 0x29 : 0x11, src, dst, uint8_t(ct), payload, size, compact, packet + 6, sizeof(packet) - 6, written);
    if (r != knxip::Result::Ok) return last_result_ = r;
    if (!routing_) return last_result_ = tunnel.send(packet + 6, written, now);
    knxip::packetHeader(packet, KNX_ST_ROUTING_INDICATION, written + 6);
#ifdef ESP32
    int ok = udp.beginMulticastPacket();
#else
    int ok = udp.beginPacketMulticast(MULTICAST_IP, MULTICAST_PORT, WiFi.localIP());
#endif
    if (!ok) return last_result_ = knxip::Result::IoError;
    size_t count = udp.write(packet, written + 6); int sent = udp.endPacket();
    if (count != written + 6 || sent != 1) return last_result_ = knxip::Result::IoError;
    routing_last_send_ = now; routing_sent_ = true; return last_result_ = knxip::Result::Ok;
}
knxip::Result ESPKNXIP::send_checked(address_t const &receiver, knx_command_type_t ct, size_t len, const uint8_t *data) {
    if (ct == KNX_CT_READ) return send_payload(receiver, ct, nullptr, 0, true);
    if (!data) return last_result_ = knxip::Result::InvalidArgument;
    if (!len || len > knxip::MaxPayload + 1) return last_result_ = knxip::Result::InvalidLength;
    if (len > 1 && data[0]) return last_result_ = knxip::Result::InvalidValue;
    return send_payload(receiver, ct, len == 1 ? data : data + 1, len == 1 ? 1 : len - 1, len == 1);
}
knxip::Result ESPKNXIP::__discovery_request(uint16_t service, IPAddress server, uint16_t port, uint16_t local_port,
                                          discovery_callback_t callback, void *arg) {
    if (!callback || !local_port || !port || local_port == MULTICAST_PORT || local_port == local_port_) return last_result_ = knxip::Result::InvalidArgument;
    if (discovery_callback_) return last_result_ = knxip::Result::Busy;
    IPAddress local = local_address();
    if (uint32_t(local) == 0) return last_result_ = knxip::Result::NotConnected;
    if (!discovery_udp.begin(local_port)) return last_result_ = knxip::Result::IoError;
    uint8_t packet[14]; knxip::packetHeader(packet, service, sizeof(packet));
    knxip::encodeHpai(packet + 6, endpoint(local, local_port));
    if (!discovery_udp.beginPacket(server, port)) { discovery_udp.stop(); return last_result_ = knxip::Result::IoError; }
    size_t count = discovery_udp.write(packet, sizeof(packet)); int sent = discovery_udp.endPacket();
    if (count != sizeof(packet) || sent != 1) { discovery_udp.stop(); return last_result_ = knxip::Result::IoError; }
    discovery_callback_ = callback; discovery_arg_ = arg; discovery_since_ = millis();
    discovery_port_ = local_port;
    discovery_service_ = service + 1; discovery_peer_ = endpoint(server, port);
    return last_result_ = knxip::Result::Ok;
}
knxip::Result ESPKNXIP::discover(discovery_callback_t callback, void *arg, uint16_t local_port) {
    return __discovery_request(0x0201, IPAddress(224,0,23,12), 3671, local_port, callback, arg);
}
knxip::Result ESPKNXIP::describe(IPAddress server, discovery_callback_t callback, void *arg, uint16_t port, uint16_t local_port) {
    if (uint32_t(server) == 0 || server[0] >= 224) return last_result_ = knxip::Result::InvalidArgument;
    return __discovery_request(0x0203, server, port, local_port, callback, arg);
}
void ESPKNXIP::__loop_discovery() {
    if (!discovery_callback_) return;
    if (uint32_t(millis() - discovery_since_) >= 10000) { discovery_callback_ = nullptr; discovery_udp.stop(); return; }
    int size = discovery_udp.parsePacket(); if (size <= 0) return;
    uint8_t data[knxip::MaxDatagram];
    if (size > int(sizeof(data))) { __clear_udp(discovery_udp); ++diagnostics_.malformed; return; }
    knxip::Endpoint sender = endpoint(discovery_udp.remoteIP(), discovery_udp.remotePort());
    int count = discovery_udp.read(data, size); __clear_udp(discovery_udp);
    if (count != size || (discovery_service_ == 0x0204 && sender != discovery_peer_)) return;
    knxip::PacketView p; knxip::DiscoveryView view;
    if (knxip::parsePacket(data, size, p) != knxip::Result::Ok || p.service != discovery_service_ ||
        knxip::parseDiscovery(p, sender, view) != knxip::Result::Ok) { ++diagnostics_.malformed; return; }
    discovery_callback_t cb = discovery_callback_; void *arg = discovery_arg_;
    if (discovery_service_ == 0x0204) { discovery_callback_ = nullptr; discovery_udp.stop(); }
    cb(view, arg);
}
