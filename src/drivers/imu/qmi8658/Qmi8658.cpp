#include "drivers/imu/qmi8658/Qmi8658.h"

#include <Arduino.h>
#include <Wire.h>
#include <algorithm>
#include <array>

namespace BoardDrivers::Qmi8658 {
    namespace {
        constexpr uint8_t kWhoAmIReg = 0x00;
        constexpr uint8_t kCtrl1Reg = 0x02;
        constexpr uint8_t kCtrl2Reg = 0x03;
        constexpr uint8_t kCtrl5Reg = 0x06;
        constexpr uint8_t kCtrl7Reg = 0x08;
        constexpr uint8_t kCtrl8Reg = 0x09;
        constexpr uint8_t kAccelStartReg = 0x35;
        constexpr uint8_t kResetReg = 0x60;
        constexpr uint8_t kResetResultReg = 0x4D;
        constexpr uint8_t kWhoAmI = 0x05;
        constexpr size_t kMaxI2cReadBytes = 32;
    } // namespace

    bool Device::probe(uint8_t address) {
        if (address > 0x7F)
            return false;
        wire_.beginTransmission(address);
        return wire_.endTransmission(true) == 0;
    }

    bool Device::readRegisters(uint8_t reg, std::span<uint8_t> bytes) {
        if (address_ > 0x7F || bytes.empty() || bytes.size() > kMaxI2cReadBytes)
            return false;
        wire_.beginTransmission(address_);
        wire_.write(reg);
        if (wire_.endTransmission(config_.releaseBusBeforeRead) != 0)
            return false;
        if (config_.releaseBusBeforeRead)
            delayMicroseconds(50);
        if (wire_.requestFrom(address_, bytes.size(), true) != bytes.size())
            return false;
        for (auto& byte : bytes)
            byte = static_cast<uint8_t>(wire_.read());
        return true;
    }

    bool Device::readRegister(uint8_t reg, uint8_t& value) {
        return readRegisters(reg, std::span{&value, size_t{1}});
    }

    bool Device::writeRegister(uint8_t reg, uint8_t value) {
        if (address_ > 0x7F)
            return false;
        wire_.beginTransmission(address_);
        wire_.write(reg);
        wire_.write(value);
        return wire_.endTransmission(true) == 0;
    }

    bool Device::updateRegister(uint8_t reg, uint8_t mask, uint8_t value) {
        uint8_t current = 0;
        return readRegister(reg, current)
            && writeRegister(reg, static_cast<uint8_t>((current & static_cast<uint8_t>(~mask)) | (value & mask)));
    }

    bool Device::begin() {
        ready_ = false;
        const std::array<uint8_t, 3> addresses{config_.address, 0x6B, 0x6A};
        for (size_t index = 0; index < addresses.size(); ++index) {
            const uint8_t candidate = addresses[index];
            if (std::find(addresses.begin(), addresses.begin() + index, candidate) != addresses.begin() + index
                || !probe(candidate))
                continue;
            address_ = candidate;
            uint8_t value = 0;
            if (!readRegister(kWhoAmIReg, value) || value != kWhoAmI || !writeRegister(kResetReg, 0xB0))
                continue;
            const uint32_t started = millis();
            while (millis() - started < 500) {
                if (readRegister(kResetResultReg, value) && value == 0x80)
                    break;
                delay(10);
            }
            if (value != 0x80 || !readRegister(kWhoAmIReg, value) || value != kWhoAmI)
                continue;
            if (!updateRegister(kCtrl1Reg, 0x40, 0x40) || !writeRegister(kCtrl8Reg, 0x80)
                || !writeRegister(kCtrl2Reg, 0x16) || !updateRegister(kCtrl5Reg, 0x07, 0x07)
                || !updateRegister(kCtrl7Reg, 0x01, 0x01))
                continue;
            ready_ = true;
            return true;
        }
        return false;
    }

    bool Device::readAcceleration(Imu::Acceleration& sample) {
        std::array<uint8_t, 6> bytes{};
        if (!ready_ || !readRegisters(kAccelStartReg, bytes))
            return false;
        sample = decodeAcceleration(bytes);
        return true;
    }
} // namespace BoardDrivers::Qmi8658
