#pragma once

#include <system_error>

#include <esp_log.h>

namespace Logger {
    void begin();
    void logResetReason();
    void checkpoint(const char* phase);
    void startupCheckpoint(const char* phase);

    inline void failure(const char* tag, const char* operation, const char* path, std::error_code code) {
        ESP_LOGE(tag, "%s failed path=%s: %s (code=%d category=%s)", operation, path, code.message().c_str(),
                 code.value(), code.category().name());
    }

    inline void failure(const char* tag, const char* operation, const char* sourcePath, const char* targetPath,
                        std::error_code code) {
        ESP_LOGE(tag, "%s failed from=%s to=%s: %s (code=%d category=%s)", operation, sourcePath, targetPath,
                 code.message().c_str(), code.value(), code.category().name());
    }
} // namespace Logger
