#include "timer/FocusOrientation.h"
#include <esp_log.h>

#include <Arduino.h>

#include <algorithm>
#include <array>
#include <cmath>

#include "board/BoardImu.h"

namespace focus {
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
        constexpr uint32_t kSampleIntervalMs = 50;
        constexpr uint32_t kStableMs = 700;
        constexpr float kSideThreshold = 0.78f;
        constexpr float kCrossLimit = 0.42f;
        constexpr float kFlatThreshold = 0.84f;

    } // namespace

    bool OrientationReader::begin() {
        if (!Board::Imu::available())
            return false;
        const std::array<uint8_t, 3> addresses = {Board::Imu::address(), 0x6B, 0x6A};
        for (size_t index = 0; index < addresses.size(); ++index) {
            const uint8_t address = addresses[index];
            const bool duplicate =
                std::ranges::find(addresses.begin(), addresses.begin() + index, address) != addresses.begin() + index;
            if (duplicate || !Board::Imu::probeAddress(address))
                continue;

            address_ = address;
            uint8_t value = 0;
            if (!Board::Imu::readRegister(address_, kWhoAmIReg, value) || value != kWhoAmI
                || !Board::Imu::writeRegister(address_, kResetReg, 0xB0))
                continue;

            const uint32_t started = millis();
            while (millis() - started < 500) {
                if (Board::Imu::readRegister(address_, kResetResultReg, value) && value == 0x80)
                    break;
                delay(10);
            }
            if (value != 0x80 || !Board::Imu::readRegister(address_, kWhoAmIReg, value) || value != kWhoAmI)
                continue;
            if (!updateRegister(kCtrl1Reg, 0x40, 0x40) || !Board::Imu::writeRegister(address_, kCtrl8Reg, 0x80)
                || !Board::Imu::writeRegister(address_, kCtrl2Reg, 0x16) || !updateRegister(kCtrl5Reg, 0x07, 0x07)
                || !updateRegister(kCtrl7Reg, 0x01, 0x01))
                continue;

            available_ = true;
            candidate_ = stable_ = Orientation::Unknown;
            candidateSinceMs_ = 0;
            lastSampleMs_ = millis() - kSampleIntervalMs;
            ESP_LOGI("focus", "IMU ready addr=0x%02X bus=%s", address_, Board::Imu::wireName());
            return true;
        }
        ESP_LOGW("focus", "IMU unavailable bus=%s", Board::Imu::wireName());
        return false;
    }

    Orientation OrientationReader::update(uint32_t nowMs) {
        if (!available_)
            return Orientation::Unknown;
        if (nowMs - lastSampleMs_ < kSampleIntervalMs)
            return stable_;
        lastSampleMs_ = nowMs;
        float x = 0;
        float y = 0;
        float z = 0;
        if (!read(x, y, z))
            return stable_;
        const Orientation measured = classify(x, y, z);
        if (measured != candidate_) {
            candidate_ = measured;
            candidateSinceMs_ = nowMs;
        } else if (nowMs - candidateSinceMs_ >= kStableMs) {
            stable_ = candidate_;
        }
        return stable_;
    }

    bool OrientationReader::updateRegister(uint8_t reg, uint8_t mask, uint8_t value) {
        uint8_t current = 0;
        return Board::Imu::readRegister(address_, reg, current)
            && Board::Imu::writeRegister(address_, reg,
                                         static_cast<uint8_t>((current & static_cast<uint8_t>(~mask))
                                                              | (value & mask)));
    }

    bool OrientationReader::read(float& x, float& y, float& z) {
        uint8_t data[6] = {};
        if (!Board::Imu::readRegisters(address_, kAccelStartReg, data, sizeof(data)))
            return false;
        const int16_t rawX = static_cast<int16_t>((data[1] << 8) | data[0]);
        const int16_t rawY = static_cast<int16_t>((data[3] << 8) | data[2]);
        const int16_t rawZ = static_cast<int16_t>((data[5] << 8) | data[4]);
        x = rawX * scale_;
        y = rawY * scale_;
        z = rawZ * scale_;
        return true;
    }

    Orientation OrientationReader::classify(float x, float y, float z) {
        if (std::fabs(z) >= kFlatThreshold && std::fabs(x) <= 0.30f && std::fabs(y) <= 0.30f)
            return Orientation::Flat;
        if (std::fabs(y) >= kSideThreshold && std::fabs(x) <= kCrossLimit && std::fabs(z) <= kCrossLimit)
            return Orientation::Flat;
        if (x >= kSideThreshold && std::fabs(y) <= kCrossLimit && std::fabs(z) <= kCrossLimit)
            return Orientation::ShortA;
        if (x <= -kSideThreshold && std::fabs(y) <= kCrossLimit && std::fabs(z) <= kCrossLimit)
            return Orientation::ShortB;
        return Orientation::Unknown;
    }

} // namespace focus
