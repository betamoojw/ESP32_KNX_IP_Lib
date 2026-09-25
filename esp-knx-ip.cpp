/**
 * esp-knx-ip library for KNX/IP communication on an ESP8266/ESP32
 * Author: Nico Weichbrodt <envy> Fix -> Schuma
 * License: MIT
 */

#include "esp-knx-ip.h"

ESPKNXIP::ESPKNXIP() : registered_callback_assignments(0), registered_callbacks(0), registered_configs(0), registered_feedbacks(0)
{
    DEBUG_PRINTLN();
    DEBUG_PRINTLN("ESPKNXIP starting up");

    // Default physical address 1.1.0
    physaddr.bytes.high = (1 << 4) | 1; // area 1, line 1
    physaddr.bytes.low = 0;             // member 0

    memset(callback_assignments, 0, MAX_CALLBACK_ASSIGNMENTS * sizeof(callback_assignment_t));
    // String members are constructed by C++; never overwrite their object storage.
    for (size_t i = 0; i < MAX_CALLBACKS; ++i) {
        callbacks[i].fkt = nullptr; callbacks[i].cond = nullptr; callbacks[i].arg = nullptr;
    }
    memset(custom_config_data, 0, MAX_CONFIG_SPACE * sizeof(uint8_t));
    memset(custom_config_default_data, 0, MAX_CONFIG_SPACE * sizeof(uint8_t));
}

void ESPKNXIP::load()
{
    memcpy(custom_config_default_data, custom_config_data, MAX_CONFIG_SPACE);
    EEPROM.begin(EEPROM_SIZE);
    restore_from_eeprom();
}

void ESPKNXIP::start()
{
    start_routing();
}

void ESPKNXIP::save_to_eeprom()
{
    uint32_t address = 0;
    uint64_t magic = EEPROM_MAGIC;
    EEPROM.put(address, magic);
    address += sizeof(uint64_t);

    EEPROM.put(address++, registered_callback_assignments);

    for (uint8_t i = 0; i < MAX_CALLBACK_ASSIGNMENTS; ++i)
    {
        EEPROM.put(address, callback_assignments[i].address);
        address += sizeof(address_t);
    }

    for (uint8_t i = 0; i < MAX_CALLBACK_ASSIGNMENTS; ++i)
    {
        EEPROM.put(address, callback_assignments[i].callback_id);
        address += sizeof(callback_id_t);
    }

    EEPROM.put(address, physaddr);
    address += sizeof(address_t);

    EEPROM.put(address, custom_config_data);
    address += sizeof(custom_config_data);

    EEPROM.commit();
    DEBUG_PRINT("Wrote to EEPROM: 0x");
    DEBUG_PRINTLN(address, HEX);
}

void ESPKNXIP::restore_from_eeprom()
{
    uint32_t address = 0;
    uint64_t magic = 0;
    EEPROM.get(address, magic);
    if (magic != EEPROM_MAGIC)
    {
        DEBUG_PRINTLN("No valid magic in EEPROM, aborting restore.");
        return;
    }

    address += sizeof(uint64_t);
    EEPROM.get(address++, registered_callback_assignments);
    if (registered_callback_assignments > MAX_CALLBACK_ASSIGNMENTS)
        registered_callback_assignments = 0;

    for (uint8_t i = 0; i < MAX_CALLBACK_ASSIGNMENTS; ++i)
    {
        EEPROM.get(address, callback_assignments[i].address);
        address += sizeof(address_t);
    }

    for (uint8_t i = 0; i < MAX_CALLBACK_ASSIGNMENTS; ++i)
    {
        EEPROM.get(address, callback_assignments[i].callback_id);
        address += sizeof(callback_id_t);
    }

    EEPROM.get(address, physaddr);
    address += sizeof(address_t);

    uint32_t conf_offset = address;
    for (uint8_t i = 0; i < registered_configs; ++i)
    {
        config_flags_t flags = (config_flags_t)EEPROM.read(address);
        DEBUG_PRINT("Flag in EEPROM @ ");
        DEBUG_PRINT(address - conf_offset);
        DEBUG_PRINT(": ");
        DEBUG_PRINTLN(flags, BIN);

        custom_config_data[custom_configs[i].offset] = flags;
        if (flags & CONFIG_FLAGS_VALUE_SET)
        {
            DEBUG_PRINTLN("Non-default value");
            for (size_t j = 0; j + sizeof(uint8_t) < custom_configs[i].len; ++j)
            {
                custom_config_data[custom_configs[i].offset + sizeof(uint8_t) + j] =
                    EEPROM.read(address + sizeof(uint8_t) + j);
            }
        }

        address += custom_configs[i].len;
    }

    DEBUG_PRINT("Restored from EEPROM: 0x");
    DEBUG_PRINTLN(address, HEX);
}

uint16_t ESPKNXIP::__ntohs(uint16_t n)
{
    return (uint16_t)((((uint8_t*)&n)[0] << 8) | (((uint8_t*)&n)[1]));
}

/**
 * Callback-Methoden
 */
callback_id_t ESPKNXIP::callback_register(String name, callback_fptr_t cb, void *arg, enable_condition_t cond)
{
    if (!cb || registered_callbacks >= MAX_CALLBACKS)
        return -1;

    callback_id_t id = registered_callbacks;

    callbacks[id].name = name;
    callbacks[id].fkt = cb;
    callbacks[id].cond = cond;
    callbacks[id].arg = arg;
    registered_callbacks++;
    return id;
}

void ESPKNXIP::callback_assign(callback_id_t id, address_t val)
{
    if (id >= registered_callbacks)
        return;

    __callback_register_assignment(val, id);
}

callback_assignment_id_t ESPKNXIP::__callback_register_assignment(address_t address, callback_id_t id)
{
    for (uint8_t i = 0; i < registered_callback_assignments; ++i)
        if (callback_assignments[i].callback_id == id && callback_assignments[i].address.value == address.value)
            return i;
    if (registered_callback_assignments >= MAX_CALLBACK_ASSIGNMENTS)
        return -1;

    callback_assignment_id_t aid = registered_callback_assignments;
    callback_assignments[aid].address = address;
    callback_assignments[aid].callback_id = id;
    registered_callback_assignments++;
    return aid;
}

void ESPKNXIP::__callback_delete_assignment(callback_assignment_id_t id)
{
    if (id >= registered_callback_assignments)
        return;
    const size_t count = registered_callback_assignments - id - 1;
    memmove(callback_assignments + id, callback_assignments + id + 1, count * sizeof(callback_assignment_t));
    registered_callback_assignments--;
}

void ESPKNXIP::callback_unassign(callback_id_t id, address_t address)
{
    for (uint8_t i = 0; i < registered_callback_assignments; ) {
        if (callback_assignments[i].callback_id == id && callback_assignments[i].address.value == address.value)
            __callback_delete_assignment(i);
        else
            ++i;
    }
}

/**
 * Feedback-Methoden
 */
feedback_id_t ESPKNXIP::feedback_register_int(String name, int32_t *value, enable_condition_t cond)
{
    if (registered_feedbacks >= MAX_FEEDBACKS)
        return -1;

    feedback_id_t id = registered_feedbacks;
    feedbacks[id].type = FEEDBACK_TYPE_INT;
    feedbacks[id].name = name;
    feedbacks[id].cond = cond;
    feedbacks[id].data = (void *)value;

    registered_feedbacks++;
    return id;
}

feedback_id_t ESPKNXIP::feedback_register_float(String name, float *value, uint8_t precision, enable_condition_t cond)
{
    if (registered_feedbacks >= MAX_FEEDBACKS)
        return -1;

    feedback_id_t id = registered_feedbacks;
    feedbacks[id].type = FEEDBACK_TYPE_FLOAT;
    feedbacks[id].name = name;
    feedbacks[id].cond = cond;
    feedbacks[id].data = (void *)value;
    feedbacks[id].options.float_options.precision = precision;

    registered_feedbacks++;
    return id;
}

feedback_id_t ESPKNXIP::feedback_register_bool(String name, bool *value, enable_condition_t cond)
{
    if (registered_feedbacks >= MAX_FEEDBACKS)
        return -1;

    feedback_id_t id = registered_feedbacks;
    feedbacks[id].type = FEEDBACK_TYPE_BOOL;
    feedbacks[id].name = name;
    feedbacks[id].cond = cond;
    feedbacks[id].data = (void *)value;

    registered_feedbacks++;
    return id;
}

feedback_id_t ESPKNXIP::feedback_register_action(String name, feedback_action_fptr_t value, void *arg, enable_condition_t cond)
{
    if (registered_feedbacks >= MAX_FEEDBACKS)
        return -1;

    feedback_id_t id = registered_feedbacks;
    feedbacks[id].type = FEEDBACK_TYPE_ACTION;
    feedbacks[id].name = name;
    feedbacks[id].cond = cond;
    feedbacks[id].data = (void *)value;
    feedbacks[id].options.action_options.arg = arg;

    registered_feedbacks++;
    return id;
}

/**
 * KNX-Loop
 */
void ESPKNXIP::loop()
{
    __routing_decay(millis());
    tunnel.tick(millis());
    __loop_knx();
    __loop_discovery();
}

void ESPKNXIP::__loop_knx()
{
    int size = udp.parsePacket();
    if (size <= 0) return;
    uint8_t buf[knxip::MaxDatagram];
    if (size > int(sizeof(buf))) { __clear_udp(udp); ++diagnostics_.malformed; return; }
    IPAddress ip = udp.remoteIP();
    knxip::Endpoint sender = {{ip[0], ip[1], ip[2], ip[3]}, udp.remotePort()};
    int count = udp.read(buf, size); __clear_udp(udp);
    if (count != size) { ++diagnostics_.malformed; return; }
    knxip::PacketView packet;
    knxip::Result r = knxip::parsePacket(buf, size, packet);
    if (r != knxip::Result::Ok) { ++diagnostics_.malformed; return; }
    knxip::CemiView frame;
    if (!routing_) {
        bool deliver;
        r = tunnel.receive(buf, size, sender, millis(), frame, deliver);
        if (r == knxip::Result::Ok && deliver) __dispatch(frame);
    } else if (sender.port == MULTICAST_PORT) {
        if (packet.service == KNX_ST_ROUTING_INDICATION) {
            r = knxip::parseCemi(packet.body, packet.size, frame);
            if (r == knxip::Result::Ok && frame.code == KNX_MT_L_DATA_IND && frame.group && !frame.confirmationError)
                __dispatch(frame);
        } else if (packet.service == KNX_ST_ROUTING_LOST_MESSAGE) {
            if (packet.size != 4 || packet.body[0] != 4) r = knxip::Result::InvalidLength;
            else diagnostics_.routing_lost += knxip::read16(packet.body + 2);
        } else if (packet.service == KNX_ST_ROUTING_BUSY) {
            if (packet.size != 6 || packet.body[0] != 6) r = knxip::Result::InvalidLength;
            else {
                uint16_t wait = knxip::read16(packet.body + 2);
                if (wait < 20 || wait > 100) r = knxip::Result::InvalidValue;
                else {
                    uint32_t now = millis();
                    if (!routing_busy_factor_ || uint32_t(now - routing_busy_at_) >= 10) {
                        if (routing_busy_factor_ < 65535) ++routing_busy_factor_;
                    }
                    routing_busy_at_ = now;
                    routing_decay_at_ = now + uint32_t(routing_busy_factor_) * 100;
                    uint32_t delay_ms = wait + uint32_t(random(0, long(routing_busy_factor_) * 50 + 1));
                    uint32_t elapsed = now - routing_wait_start_;
                    uint32_t remaining = elapsed < routing_wait_ms_ ? routing_wait_ms_ - elapsed : 0;
                    routing_wait_start_ = now; routing_wait_ms_ = delay_ms > remaining ? delay_ms : remaining;
                    ++diagnostics_.routing_busy;
                }
            }
        }
    }
    if (r == knxip::Result::InvalidLength || r == knxip::Result::InvalidValue) ++diagnostics_.malformed;
}

void ESPKNXIP::__dispatch(const knxip::CemiView &frame)
{
    address_t destination = {}, source = {};
    destination.bytes.high = uint8_t(frame.destination >> 8); destination.bytes.low = uint8_t(frame.destination);
    source.bytes.high = uint8_t(frame.source >> 8); source.bytes.low = uint8_t(frame.source);
    for (size_t i = 0; i < registered_callback_assignments; ++i) {
        callback_id_t id = callback_assignments[i].callback_id;
        if (id >= registered_callbacks || !callbacks[id].fkt || callback_assignments[i].address.value != destination.value) continue;
        if (!callbacks[id].cond || callbacks[id].cond()) {
            uint8_t data[knxip::MaxPayload + 1]; data[0] = frame.compact;
            if (frame.size) memcpy(data + 1, frame.payload, frame.size);
            message_t msg = {};
            msg.ct = knx_command_type_t(frame.command); msg.received_on = destination;
            msg.source = source; msg.data_len = uint8_t(frame.size + 1); msg.data = data;
            callbacks[id].fkt(msg, callbacks[id].arg);
        }
#if !ALLOW_MULTIPLE_CALLBACKS_PER_ADDRESS
        return;
#endif
    }
}

/**
 * Neuer READ Helper für externe GAs
 */
void ESPKNXIP::send_ext(address_t const &receiver)
{
    uint8_t data[1] = {0x00};  // TPCI: READ, APCI automatisch auf READ
    send(receiver, KNX_CT_READ, 1, data);  // Länge = 1, Daten = [0x00]
}


// Global "singleton" object
ESPKNXIP knx;
