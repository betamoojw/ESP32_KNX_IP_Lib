#ifndef TEST_ARDUINO_H
#define TEST_ARDUINO_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#define HEX 16
#define BIN 2
#define F(x) x
class String {
    char text[256];
public:
    String(const char *value = "") { size_t n = strlen(value); if (n > 255) n = 255; memcpy(text,value,n); text[n] = 0; }
    size_t length() const { return strlen(text); }
    const char *c_str() const { return text; }
};
struct SerialStub {
    template<class... T> void print(T...) {}
    template<class... T> void println(T...) {}
};
extern SerialStub Serial;
extern uint32_t test_now;
inline uint32_t millis() { return test_now; }
inline long random(long lo, long hi) { return lo + (hi-lo)/2; }
#endif
