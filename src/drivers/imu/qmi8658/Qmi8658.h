#pragma once

#include <bit>
#include <cstdint>
#include <span>

#include "drivers/imu/ImuTypes.h"

class TwoWire;

namespace BoardDrivers::Qmi8658 {
    struct Config {
        uint8_t address;
        bool releaseBusBeforeRead;
        const char* wireName;
    };

    // CTRL2 = 0x16 selects the existing +/-4 g range. Keep decoding and setup together.
    constexpr Imu::Acceleration decodeAcceleration(std::span<const uint8_t, 6> bytes) {
        const auto axis = [](uint8_t low, uint8_t high) {
            const auto bits = static_cast<uint16_t>(low | (static_cast<uint16_t>(high) << 8));
            return std::bit_cast<int16_t>(bits) * (4.0f / 32768.0f);
        };
        return {axis(bytes[0], bytes[1]), axis(bytes[2], bytes[3]), axis(bytes[4], bytes[5])};
    }

    class Device {
    public:
        Device(TwoWire& wire, Config config) : wire_(wire), config_(config), address_(config.address) {}
        Device(const Device&) = delete;
        Device& operator=(const Device&) = delete;

        bool begin();
        bool readAcceleration(Imu::Acceleration& sample);
        uint8_t address() const { return address_; }
        const char* wireName() const { return config_.wireName; }

    private:
        bool probe(uint8_t address);
        bool readRegister(uint8_t reg, uint8_t& value);
        bool writeRegister(uint8_t reg, uint8_t value);
        bool updateRegister(uint8_t reg, uint8_t mask, uint8_t value);
        bool readRegisters(uint8_t reg, std::span<uint8_t> bytes);

        TwoWire& wire_;
        Config config_;
        uint8_t address_;
        bool ready_ = false;
    };
} // namespace BoardDrivers::Qmi8658
