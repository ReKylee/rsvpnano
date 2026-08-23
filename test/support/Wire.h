#pragma once

#include <cstddef>
#include <cstdint>

class TwoWire {
public:
    void beginTransmission(uint8_t) {}
    uint8_t endTransmission(bool = true) { return 0; }
    size_t write(const uint8_t*, size_t length) { return length; }
    size_t requestFrom(uint8_t, size_t length, bool) { return length; }
    int read() { return 0; }
};
