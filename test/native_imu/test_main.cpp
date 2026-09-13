#include <Arduino.h>
#include <Wire.h>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <type_traits>
#include "drivers/imu/qmi8658/Qmi8658.h"

using BoardDrivers::Qmi8658::Device;
using BoardDrivers::Imu::Acceleration;

void testDecode() {
    constexpr std::array<uint8_t, 6> bytes{0x00, 0x20, 0x00, 0xE0, 0x00, 0x80};
    constexpr auto sample = BoardDrivers::Qmi8658::decodeAcceleration(bytes);
    static_assert(sample.x == 1.0f && sample.y == -1.0f && sample.z == -4.0f);
    constexpr std::array<uint8_t, 6> edge{0xFF, 0x7F, 0x01, 0x00, 0xFF, 0xFF};
    constexpr auto maximum = BoardDrivers::Qmi8658::decodeAcceleration(edge);
    static_assert(maximum.x == 32767 * (4.0f / 32768.0f));
    static_assert(maximum.y == 4.0f / 32768.0f && maximum.z == -4.0f / 32768.0f);
    static_assert(!std::is_copy_constructible_v<Device>);
}

void testSequence(bool stop) {
    TwoWire wire;
    wire.registers[0x02] = 0x03;
    wire.registers[0x06] = 0xA0;
    wire.registers[0x08] = 0x80;
    Device sensor{wire, {0x6B, stop, "fake"}};
    assert(wire.transfers.empty());
    Acceleration sample{8, 9, 10};
    assert(!sensor.readAcceleration(sample));
    assert(wire.transfers.empty() && sample.x == 8);
    testReadDelays = 0;
    assert(sensor.begin());
    const std::vector<std::vector<uint8_t>> expected{
        {0x60, 0xB0}, {0x02, 0x43}, {0x09, 0x80}, {0x03, 0x16}, {0x06, 0xA7}, {0x08, 0x81}};
    std::vector<std::vector<uint8_t>> writes;
    for (const auto& transfer : wire.transfers) {
        if (transfer.bytes.size() == 2) {
            assert(transfer.stop);
            writes.push_back(transfer.bytes);
        } else if (transfer.bytes.size() == 1) {
            assert(transfer.stop == stop);
        }
    }
    assert(writes == expected);
    assert(testReadDelays == (stop ? wire.reads : 0));
    wire.registers[0x35] = 0;
    wire.registers[0x36] = 0x20;
    wire.registers[0x37] = 0;
    wire.registers[0x38] = 0xE0;
    wire.registers[0x39] = 0;
    wire.registers[0x3A] = 0x40;
    assert(sensor.readAcceleration(sample));
    assert(sample.x == 1 && sample.y == -1 && sample.z == 2);
    wire.shortRead = true;
    assert(!sensor.readAcceleration(sample));
    assert(sample.x == 1 && sample.y == -1 && sample.z == 2);
    wire.shortRead = false;
    wire.connected = false;
    assert(!sensor.begin());
    assert(!sensor.readAcceleration(sample));
}

void testFallback() {
    TwoWire wire;
    wire.respondingAddress = 0x6A;
    Device sensor{wire, {0x6B, true, "fake"}};
    assert(sensor.begin() && sensor.address() == 0x6A);
    unsigned configuredProbes = 0;
    for (const auto& transfer : wire.transfers)
        configuredProbes += transfer.address == 0x6B && transfer.bytes.empty();
    assert(configuredProbes == 1);
}

void testTimeoutAndWrongDevice() {
    TwoWire wire;
    wire.registers[0x4D] = 0;
    Device sensor{wire, {0x6B, true, "fake"}};
    testClockMs = std::numeric_limits<uint32_t>::max() - 100;
    const uint32_t started = testClockMs;
    assert(!sensor.begin());
    assert(testClockMs - started == 500);
    wire.registers[0x00] = 0xFF;
    wire.transfers.clear();
    assert(!sensor.begin());
    for (const auto& transfer : wire.transfers)
        assert(transfer.bytes.size() < 2);
}

int main() {
    testDecode(); testSequence(false); testSequence(true);
    testFallback(); testTimeoutAndWrongDevice();
    std::cout << "QMI8658: decode, setup, transport policy, fallback, failure and timeout tests passed\n";
}
