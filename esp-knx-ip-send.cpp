/**
 * esp-knx-ip library for KNX/IP communication on an ESP8266
 * Author: Nico Weichbrodt <envy>
 * License: MIT
 */

#include "esp-knx-ip.h"

/**
 * Send functions
 */

void ESPKNXIP::send(address_t const &receiver, knx_command_type_t ct, uint8_t data_len, uint8_t *data)
{
    send_checked(receiver, ct, data_len, data);
}

void ESPKNXIP::send_1bit(address_t const &receiver, knx_command_type_t ct, uint8_t bit)
{
	uint8_t buf[] = {(uint8_t)(bit & 0b00000001)};
	send(receiver, ct, 1, buf);
}

void ESPKNXIP::send_2bit(address_t const &receiver, knx_command_type_t ct, uint8_t twobit)
{
	uint8_t buf[] = {(uint8_t)(twobit & 0b00000011)};
	send(receiver, ct, 1, buf);
}

void ESPKNXIP::send_4bit(address_t const &receiver, knx_command_type_t ct, uint8_t fourbit)
{
	uint8_t buf[] = {(uint8_t)(fourbit & 0b00001111)};
	send(receiver, ct, 1, buf);
}

void ESPKNXIP::send_1byte_int(address_t const &receiver, knx_command_type_t ct, int8_t val)
{
	uint8_t buf[] = {0x00, (uint8_t)val};
	send(receiver, ct, 2, buf);
}

void ESPKNXIP::send_1byte_uint(address_t const &receiver, knx_command_type_t ct, uint8_t val)
{
	uint8_t buf[] = {0x00, val};
	send(receiver, ct, 2, buf);
}

void ESPKNXIP::send_2byte_int(address_t const &receiver, knx_command_type_t ct, int16_t val)
{
	uint8_t buf[] = {0x00, (uint8_t)(val >> 8), (uint8_t)(val & 0x00FF)};
	send(receiver, ct, 3, buf);
}

void ESPKNXIP::send_2byte_uint(address_t const &receiver, knx_command_type_t ct, uint16_t val)
{
	uint8_t buf[] = {0x00, (uint8_t)(val >> 8), (uint8_t)(val & 0x00FF)};
	send(receiver, ct, 3, buf);
}

void ESPKNXIP::send_2byte_float(address_t const &receiver, knx_command_type_t ct, float val)
{
    uint8_t buf[2];
    last_result_ = knxip::dpt::encodeFloat16(val, buf, sizeof(buf));
    if (last_result_ == knxip::Result::Ok) send_payload(receiver, ct, buf, sizeof(buf));
}

void ESPKNXIP::send_3byte_time(address_t const &receiver, knx_command_type_t ct, uint8_t weekday, uint8_t hours, uint8_t minutes, uint8_t seconds)
{
    uint8_t buf[3];
    knxip::dpt::Time value = {weekday, hours, minutes, seconds};
    last_result_ = knxip::dpt::encodeTime(value, buf, sizeof(buf));
    if (last_result_ == knxip::Result::Ok) send_payload(receiver, ct, buf, sizeof(buf));
}

void ESPKNXIP::send_3byte_date(address_t const &receiver, knx_command_type_t ct, uint8_t day, uint8_t month, uint8_t year)
{
    uint8_t buf[3];
    if (year > 99) { last_result_ = knxip::Result::OutOfRange; return; }
    knxip::dpt::Date value = {uint16_t((year >= 90 ? 1900 : 2000) + year), month, day};
    last_result_ = knxip::dpt::encodeDate(value, buf, sizeof(buf));
    if (last_result_ == knxip::Result::Ok) send_payload(receiver, ct, buf, sizeof(buf));
}

void ESPKNXIP::send_3byte_color(address_t const &receiver, knx_command_type_t ct, uint8_t red, uint8_t green, uint8_t blue)
{
	uint8_t buf[] = {0x00, red, green, blue};
	send(receiver, ct, 4, buf);
}

void ESPKNXIP::send_4byte_int(address_t const &receiver, knx_command_type_t ct, int32_t val)
{
	uint8_t buf[] = {0x00,
	                 (uint8_t)((val & 0xFF000000) >> 24),
	                 (uint8_t)((val & 0x00FF0000) >> 16),
	                 (uint8_t)((val & 0x0000FF00) >> 8),
	                 (uint8_t)((val & 0x000000FF) >> 0)};
	send(receiver, ct, 5, buf);
}

void ESPKNXIP::send_4byte_uint(address_t const &receiver, knx_command_type_t ct, uint32_t val)
{
	uint8_t buf[] = {0x00,
	                 (uint8_t)((val & 0xFF000000) >> 24),
	                 (uint8_t)((val & 0x00FF0000) >> 16),
	                 (uint8_t)((val & 0x0000FF00) >> 8),
	                 (uint8_t)((val & 0x000000FF) >> 0)};
	send(receiver, ct, 5, buf);
}

void ESPKNXIP::send_4byte_float(address_t const &receiver, knx_command_type_t ct, float val)
{
    uint8_t buf[4];
    last_result_ = knxip::dpt::encodeFloat32(val, buf, sizeof(buf));
    if (last_result_ == knxip::Result::Ok) send_payload(receiver, ct, buf, sizeof(buf));
}

void ESPKNXIP::send_14byte_string(address_t const &receiver, knx_command_type_t ct, const char *val)
{
    uint8_t buf[14];
    last_result_ = knxip::dpt::encodeString14(val, buf, sizeof(buf), false);
    if (last_result_ == knxip::Result::Ok) send_payload(receiver, ct, buf, sizeof(buf));
}
