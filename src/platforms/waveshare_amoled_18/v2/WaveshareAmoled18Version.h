#pragma once

#include "ui/Touch.h"

namespace WaveshareAmoled18::Version {

    constexpr const char* kBoardId = "waveshare_esp32s3_touch_amoled_1_8_v2";
    constexpr const char* kBoardLabel = "Waveshare ESP32-S3-Touch-AMOLED-1.8 V2";
    constexpr const char* kOtaAssetName = "rsvp-nano-esp32-s3-touch-amoled-1.8-v2-ota.bin";

    // PR #116 showed that CO5300 MADCTL was not a reliable fix for the v2 panel
    // orientation. Keep hardware panel memory unrotated and make the UI orientation
    // the board-version fact instead of adding shared App/Input conditionals.
    constexpr bool kPanelMemoryRotated180 = false;
    constexpr uint16_t kPanelColumnOffset = 16;
    constexpr uint16_t kPanelRowOffset = 0;
    constexpr ui::Orientation kDefaultUiOrientation = ui::Orientation::LandscapeFlipped;

    constexpr uint8_t kTouchAddress = 0x15;

} // namespace WaveshareAmoled18::Version
