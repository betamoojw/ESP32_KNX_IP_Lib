#ifndef KNX_PROTOCOL_H
#define KNX_PROTOCOL_H
#include "knx-codec.h"

namespace knxip {
static const size_t MaxPayload = 254;
static const size_t MaxCemi = 265;
// Includes room for all 255 additional-info bytes on receive.
static const size_t MaxDatagram = 530;
struct PacketView { uint16_t service; const uint8_t *body; size_t size; };
struct CemiView {
    uint8_t code, command, compact, hopCount;
    uint16_t source, destination;
    const uint8_t *payload;
    size_t size;
    bool group, confirmationError;
};
Result parsePacket(const uint8_t *data, size_t len, PacketView &packet);
void packetHeader(uint8_t *out, uint16_t service, size_t length);
Result parseCemi(const uint8_t *data, size_t len, CemiView &frame);
Result encodeCemi(uint8_t code, uint16_t source, uint16_t destination, uint8_t command,
                  const uint8_t *payload, size_t size, bool compact,
                  uint8_t *out, size_t capacity, size_t &written);

struct Endpoint {
    uint8_t ip[4]; uint16_t port;
    bool operator==(const Endpoint &other) const;
    bool operator!=(const Endpoint &other) const { return !(*this == other); }
};
void encodeHpai(uint8_t *out, const Endpoint &endpoint);
Result parseHpai(const uint8_t *data, size_t len, Endpoint &endpoint);
struct DiscoveryView {
    Endpoint control;
    const uint8_t *dibs;
    size_t size;
};
Result parseDiscovery(const PacketView &packet, const Endpoint &sender, DiscoveryView &view);
// Call only on the validated DIB area returned by parseDiscovery.
const uint8_t *findDib(const DiscoveryView &view, uint8_t type, size_t &size);

class Tunnel {
public:
    enum class State { Disconnected, Connecting, Connected, Disconnecting };
    typedef bool (*Transmit)(const Endpoint &, const uint8_t *, size_t, void *);
    Tunnel();
    Result connect(const Endpoint &local, const Endpoint &server, uint32_t now,
                   Transmit transmit, void *context, bool nat = false);
    Result disconnect(uint32_t now);
    Result send(const uint8_t *cemi, size_t len, uint32_t now);
    // A returned frame references data and is valid only during the caller's receive cycle.
    Result receive(const uint8_t *data, size_t len, const Endpoint &sender,
                   uint32_t now, CemiView &frame, bool &deliver);
    void tick(uint32_t now);
    void reset();
    State state() const { return state_; }
    bool pending() const { return pending_; }
    uint16_t address() const { return address_; }
    uint8_t lastStatus() const { return status_; }
    Result lastResult() const { return result_; }
private:
    bool emit(const Endpoint &ep, const uint8_t *data, size_t len);
    void control(uint16_t service);
    void retry(uint32_t now);
    State state_;
    Endpoint local_, server_, data_;
    Transmit transmit_;
    void *context_;
    uint8_t channel_, txSequence_, rxSequence_, retries_, heartRetries_, status_;
    uint16_t address_;
    bool pending_, heartbeat_, received_;
    uint32_t stateSince_, sentAt_, heartAt_, lastHeart_;
    uint8_t pendingData_[MaxCemi + 10];
    size_t pendingSize_;
    Result result_;
};
} // namespace knxip
#endif
