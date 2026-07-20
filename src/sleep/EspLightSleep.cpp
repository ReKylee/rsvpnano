#include "sleep/EspLightSleep.h"

#include <Arduino.h>
#include <esp_sleep.h>

#include <esp_log.h>

namespace EspLightSleep {

    WakeReason wait(std::span<const gpio_num_t> wakePins, uint32_t timeoutMs) {
        const uint32_t releaseWaitStartedMs = millis();
        while (millis() - releaseWaitStartedMs < 1000) {
            bool released = true;
            for (const gpio_num_t pin : wakePins) {
                pinMode(static_cast<int>(pin), INPUT_PULLUP);
                released = released && digitalRead(static_cast<int>(pin));
            }
            if (released)
                break;
            delay(10);
        }

        for (const gpio_num_t pin : wakePins) {
            if (const esp_err_t error = gpio_wakeup_enable(pin, GPIO_INTR_LOW_LEVEL); error != ESP_OK) {
                ESP_LOGE("sleep", "failed to arm GPIO %d for light sleep: %d", static_cast<int>(pin),
                              static_cast<int>(error));
                for (const gpio_num_t cleanupPin : wakePins)
                    gpio_wakeup_disable(cleanupPin);
                return WakeReason::error;
            }
        }

        const esp_err_t gpioError = esp_sleep_enable_gpio_wakeup();
        const esp_err_t timerError = timeoutMs == 0
            ? ESP_OK
            : esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(timeoutMs) * 1000ULL);
        if (gpioError != ESP_OK || timerError != ESP_OK) {
            ESP_LOGE("sleep", "failed to arm light sleep: gpio=%d timer=%d", static_cast<int>(gpioError),
                          static_cast<int>(timerError));
            for (const gpio_num_t pin : wakePins)
                gpio_wakeup_disable(pin);
            esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
            if (timeoutMs > 0)
                esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
            return WakeReason::error;
        }

        const esp_err_t sleepError = esp_light_sleep_start();
        const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

        for (const gpio_num_t pin : wakePins)
            gpio_wakeup_disable(pin);
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
        if (timeoutMs > 0)
            esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);

        if (sleepError != ESP_OK) {
            ESP_LOGE("sleep", "light sleep failed: %d", static_cast<int>(sleepError));
            return WakeReason::error;
        }
        if (cause == ESP_SLEEP_WAKEUP_GPIO)
            return WakeReason::input;
        if (cause == ESP_SLEEP_WAKEUP_TIMER)
            return WakeReason::timer;
        return WakeReason::other;
    }

} // namespace EspLightSleep
