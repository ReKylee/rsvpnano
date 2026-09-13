#include "board/BoardImu.h"

#include <Arduino.h>
#include <Wire.h>
#include <cassert>
#include <iostream>
#include <string_view>

TwoWire Wire;
#if SOC_I2C_NUM > 1
TwoWire Wire1;
#endif

int main() {
    assert(Wire.transfers.empty());
#if SOC_I2C_NUM > 1
    assert(Wire1.transfers.empty());
#endif
    Board::Imu::Acceleration sample{7, 8, 9};
    assert(!Board::Imu::readAcceleration(sample));
    assert(sample.x == 7 && sample.y == 8 && sample.z == 9);
    assert(Wire.transfers.empty());
#if SOC_I2C_NUM > 1
    assert(Wire1.transfers.empty());
#endif
    assert(Board::Imu::begin());
#if TEST_IMU_BUS == 1
    auto& selected = Wire1;
    assert(Wire.transfers.empty());
    assert(std::string_view{Board::Imu::wireName()} == "Wire1");
#else
    auto& selected = Wire;
#if SOC_I2C_NUM > 1
    assert(Wire1.transfers.empty());
#endif
    assert(std::string_view{Board::Imu::wireName()} == "Wire");
#endif
    assert(!selected.transfers.empty());
    assert(Board::Imu::address() == 0x6B);
    selected.registers[0x36] = 0x20;
    selected.registers[0x38] = 0xE0;
    selected.registers[0x3A] = 0x40;
    assert(Board::Imu::readAcceleration(sample));
    assert(sample.x == 1 && sample.y == -1 && sample.z == 2);
    selected.shortRead = true;
    assert(!Board::Imu::readAcceleration(sample));
    assert(sample.x == 1 && sample.y == -1 && sample.z == 2);
    std::cout << "QMI8658 board adapter: Wire" << TEST_IMU_BUS << " passed\n";
}
