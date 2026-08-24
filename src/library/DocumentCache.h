#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <system_error>

namespace DocumentCache {

    using StatusCallback = void (*)(void* context, const char* title, const char* line1, const char* line2,
                                    int progressPercent);

    bool rsvpIsCurrent(std::string_view documentPath, std::string_view rsvpPath);
    bool hasCurrentCache(std::string_view documentPath);
    std::string libraryLabel(std::string_view documentPath);
    std::expected<std::string, std::error_code> ensureConverted(std::string_view documentPath,
                                                                StatusCallback statusCallback, void* statusContext);

} // namespace DocumentCache
