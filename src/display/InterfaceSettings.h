#pragma once

#include <cstdint>

#include "standby/ScreensaverTypes.h"
#include "ui/Localization.h"

struct InterfaceSettings {
    uint8_t brightnessIndex = 3;
    uint8_t standbyIndex = 1;
    UiLanguage language = UiLanguage::English;
    standby::Kind screensaver = standby::Kind::Life;
};
