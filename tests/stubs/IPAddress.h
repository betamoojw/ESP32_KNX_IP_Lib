#pragma once
#include "Arduino.h"
class IPAddress {
    uint8_t bytes[4];
public:
    IPAddress(uint8_t a=0,uint8_t b=0,uint8_t c=0,uint8_t d=0) : bytes{a,b,c,d} {}
    uint8_t operator[](size_t i) const { return bytes[i]; }
    operator uint32_t() const { return uint32_t(bytes[0]) | (uint32_t(bytes[1])<<8) | (uint32_t(bytes[2])<<16) | (uint32_t(bytes[3])<<24); }
};
