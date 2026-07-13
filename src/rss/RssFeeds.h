#pragma once

#include <Arduino.h>
#include <Preferences.h>

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

    Result check(Preferences& preferences, StatusCallback callback = nullptr, void* context = nullptr);

} // namespace RssFeeds
