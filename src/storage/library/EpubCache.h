#pragma once

#include <Arduino.h>
#include <expected>
#include <system_error>

namespace EpubCache {

    using StatusCallback = void (*)(void* context, const char* title, const char* line1, const char* line2,
                                    int progressPercent);

    bool rsvpIsCurrent(const String& rsvpPath);
    bool hasCurrentCache(const String& epubPath);
    String libraryLabel(const String& epubPath);
    std::expected<String, std::error_code> ensureConverted(const String& epubPath, StatusCallback statusCallback,
                                                           void* statusContext);

} // namespace EpubCache
