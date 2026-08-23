#pragma once

#include <system_error>

#include <Arduino.h>
#include <esp_log.h>
#include <esp_system.h>

namespace Logger {
    inline void begin() {
        Serial.setDebugOutput(true);
    }

    namespace detail {
        inline const char* resetReasonName(esp_reset_reason_t reason) {
            switch (reason) {
            case ESP_RST_POWERON:
                return "poweron";
            case ESP_RST_EXT:
                return "external";
            case ESP_RST_SW:
                return "software";
            case ESP_RST_PANIC:
                return "panic";
            case ESP_RST_INT_WDT:
                return "interrupt_watchdog";
            case ESP_RST_TASK_WDT:
                return "task_watchdog";
            case ESP_RST_WDT:
                return "watchdog";
            case ESP_RST_BROWNOUT:
                return "brownout";
            case ESP_RST_SDIO:
                return "sdio";
            case ESP_RST_UNKNOWN:
            default:
                return "unknown";
            }
        }
    } // namespace detail

    inline void logResetReason() {
        const esp_reset_reason_t reason = esp_reset_reason();
        ESP_LOGD("diag", "reset=%s(%d)", detail::resetReasonName(reason), static_cast<int>(reason));
    }

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
