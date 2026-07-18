#pragma once

#include <cstdarg>
#include <cstring>
#include <system_error>

#include <esp_log.h>

namespace Logger {
    namespace detail {
        inline void write(esp_log_level_t level, const char* tag, const char* format, va_list args) {
            esp_log_va(ESP_LOG_CONFIG_INIT(level | ESP_LOG_CONFIGS_DEFAULT), tag, format, args);
        }
    } // namespace detail

    inline void begin(esp_log_level_t level = ESP_LOG_INFO) {
        esp_log_level_set("*", level);
    }

    inline void debug(const char* tag, const char* format, ...) __attribute__((format(printf, 2, 3)));
    inline void debug(const char* tag, const char* format, ...) {
        va_list args;
        va_start(args, format);
        detail::write(ESP_LOG_DEBUG, tag, format, args);
        va_end(args);
    }

    inline void info(const char* tag, const char* format, ...) __attribute__((format(printf, 2, 3)));
    inline void info(const char* tag, const char* format, ...) {
        va_list args;
        va_start(args, format);
        detail::write(ESP_LOG_INFO, tag, format, args);
        va_end(args);
    }

    inline void warning(const char* tag, const char* format, ...) __attribute__((format(printf, 2, 3)));
    inline void warning(const char* tag, const char* format, ...) {
        va_list args;
        va_start(args, format);
        detail::write(ESP_LOG_WARN, tag, format, args);
        va_end(args);
    }

    inline void error(const char* tag, const char* format, ...) __attribute__((format(printf, 2, 3)));
    inline void error(const char* tag, const char* format, ...) {
        va_list args;
        va_start(args, format);
        detail::write(ESP_LOG_ERROR, tag, format, args);
        va_end(args);
    }

    inline void failure(const char* tag, const char* operation, const char* path, int code) {
        if (code == 0) {
            error(tag, "%s failed path=%s errno=0", operation, path);
            return;
        }
        error(tag, "%s failed path=%s errno=%d (%s)", operation, path, code, std::strerror(code));
    }

    inline void failure(const char* tag, const char* operation, const char* sourcePath, const char* targetPath,
                        int code) {
        if (code == 0) {
            error(tag, "%s failed from=%s to=%s errno=0", operation, sourcePath, targetPath);
            return;
        }
        error(tag, "%s failed from=%s to=%s errno=%d (%s)", operation, sourcePath, targetPath, code,
              std::strerror(code));
    }

    inline void failure(const char* tag, const char* operation, const char* path, std::error_code code) {
        error(tag, "%s failed path=%s: %s (code=%d category=%s)", operation, path, code.message().c_str(), code.value(),
              code.category().name());
    }

    inline void failure(const char* tag, const char* operation, const char* sourcePath, const char* targetPath,
                        std::error_code code) {
        error(tag, "%s failed from=%s to=%s: %s (code=%d category=%s)", operation, sourcePath, targetPath,
              code.message().c_str(), code.value(), code.category().name());
    }
} // namespace Logger
