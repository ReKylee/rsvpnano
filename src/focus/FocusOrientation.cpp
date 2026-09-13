#include "focus/FocusOrientation.h"

#include <Arduino.h>
#include <esp_log.h>
#include <cmath>
#include "board/BoardImu.h"

namespace focus {
    namespace {
        constexpr uint32_t kSampleIntervalMs = 50;
        constexpr uint32_t kStableMs = 700;
        constexpr float kSideThreshold = 0.78f;
        constexpr float kCrossLimit = 0.42f;
        constexpr float kFlatThreshold = 0.84f;
    } // namespace

    bool OrientationReader::begin() {
        available_ = Board::Imu::begin();
        if (!available_) {
            ESP_LOGW("focus", "IMU unavailable bus=%s", Board::Imu::wireName());
            return false;
        }
        candidate_ = stable_ = Orientation::Unknown;
        candidateSinceMs_ = 0;
        lastSampleMs_ = millis() - kSampleIntervalMs;
        ESP_LOGI("focus", "IMU ready addr=0x%02X bus=%s", Board::Imu::address(), Board::Imu::wireName());
        return true;
    }

    Orientation OrientationReader::update(uint32_t nowMs) {
        if (!available_)
            return Orientation::Unknown;
        if (nowMs - lastSampleMs_ < kSampleIntervalMs)
            return stable_;
        lastSampleMs_ = nowMs;
        Board::Imu::Acceleration sample;
        if (!Board::Imu::readAcceleration(sample))
            return stable_;
        const Orientation measured = classify(sample.x, sample.y, sample.z);
        if (measured != candidate_) {
            candidate_ = measured;
            candidateSinceMs_ = nowMs;
        } else if (nowMs - candidateSinceMs_ >= kStableMs) {
            stable_ = candidate_;
        }
        return stable_;
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
