#pragma once

#include <Arduino.h>

namespace Board::Imu {

    bool available();
    const char* wireName();
    uint8_t address();
    bool probeAddress(uint8_t address);
    bool readRegister(uint8_t address, uint8_t reg, uint8_t& value);
    bool writeRegister(uint8_t address, uint8_t reg, uint8_t value);
    bool readRegisters(uint8_t address, uint8_t startReg, uint8_t* buffer, size_t len);

} // namespace Board::Imu
