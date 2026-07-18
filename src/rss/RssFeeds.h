#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include "settings/SettingsModel.h"

namespace RssFeeds {

    using StatusCallback = void (*)(void* context, const char* title, const char* line1, const char* line2,
                                    int progressPercent);

    struct Result {
        uint8_t feedsChecked = 0;
        uint8_t articlesSaved = 0;
        uint8_t articlesSkipped = 0;
        String summary;
        String detail;
    };

    Result check(Preferences& statePreferences, const settings::DeviceSettings& settings,
                 const settings::DeviceSecrets& secrets, StatusCallback callback = nullptr, void* context = nullptr);

} // namespace RssFeeds
