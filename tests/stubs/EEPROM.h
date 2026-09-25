#ifndef TEST_EEPROM_H
#define TEST_EEPROM_H
#include "Arduino.h"
struct EEPROMStub {
    uint8_t data[EEPROM_SIZE] = {};
    void begin(size_t) {}
    template<class T> void put(size_t pos, const T &v) { memcpy(data+pos,&v,sizeof(v)); }
    template<class T> void get(size_t pos, T &v) { memcpy(&v,data+pos,sizeof(v)); }
    uint8_t read(size_t pos) { return data[pos]; }
    void commit() {}
};
extern EEPROMStub EEPROM;
#endif
