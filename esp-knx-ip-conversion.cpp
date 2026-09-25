/**
 * esp-knx-ip library for KNX/IP communication on an ESP8266
 * Author: Nico Weichbrodt <envy>
 * License: MIT
 */

#include "esp-knx-ip.h"

/**
 * Conversion functions
 */

bool ESPKNXIP::data_to_bool(uint8_t *data)
{
	return (data[0] & 0x01) == 1 ? true : false;
}

int8_t ESPKNXIP::data_to_1byte_int(uint8_t *data)
{
	return (int8_t)data[1];
}

uint8_t ESPKNXIP::data_to_1byte_uint(uint8_t *data)
{
	return data[1];
}

int16_t ESPKNXIP::data_to_2byte_int(uint8_t *data)
{
	return (int16_t)((data[1] << 8) | data[2]);
}

uint16_t ESPKNXIP::data_to_2byte_uint(uint8_t *data)
{
	return (uint16_t)((data[1] << 8) | data[2]);
}

float ESPKNXIP::data_to_2byte_float(uint8_t *data)
{
    float value = NAN;
    if (data) knxip::dpt::decodeFloat16(data + 1, 2, value);
    return value;
}

time_of_day_t ESPKNXIP::data_to_3byte_time(uint8_t *data)
{
	time_of_day_t time;
	time.weekday = (weekday_t)((data[1] & 0b11100000) >> 5);
	time.hours = (data[1] & 0b00011111);
	time.minutes = (data[2] & 0b00111111);
	time.seconds = (data[3] & 0b00111111);
	return time;
}

date_t ESPKNXIP::data_to_3byte_data(uint8_t *data)
{
	date_t date;
	date.day = (data[1] & 0b00011111);
	date.month = (data[2] & 0b00001111);
	date.year = (data[3] & 0b01111111);
	return date;
}

color_t ESPKNXIP::data_to_3byte_color(uint8_t *data)
{
	color_t color;
	color.red = data[1];
	color.green = data[2];
	color.blue = data[3];
	return color;
}

int32_t ESPKNXIP::data_to_4byte_int(uint8_t *data)
{
    int64_t value = 0;
    if (data) knxip::dpt::decodeSigned(data + 1, 4, value);
    return int32_t(value);
}

uint32_t ESPKNXIP::data_to_4byte_uint(uint8_t *data)
{
    return data ? knxip::read32(data + 1) : 0;
}

float ESPKNXIP::data_to_4byte_float(uint8_t *data)
{
    float value = NAN;
    if (data) knxip::dpt::decodeFloat32(data + 1, 4, value);
    return value;
}
