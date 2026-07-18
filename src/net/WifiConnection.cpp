#include "net/WifiConnection.h"

#include <WiFi.h>

namespace net {
    namespace {

        constexpr uint32_t kWifiConnectPollMs = 250;

    } // namespace

    std::expected<void, std::error_code> connectStation(const char* ssid, const char* password,
                                                        const WifiProgress& progress, uint32_t timeoutMs) {
        if (ssid == nullptr || *ssid == '\0' || timeoutMs == 0)
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        WiFi.persistent(false);
        WiFi.setAutoReconnect(false);
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid, password);

        const uint32_t startMs = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startMs < timeoutMs) {
            if (progress) {
                const uint32_t elapsedMs = millis() - startMs;
                progress(5 + static_cast<int>((elapsedMs * 15) / timeoutMs));
            }
            delay(kWifiConnectPollMs);
        }

        if (WiFi.status() != WL_CONNECTED)
            return std::unexpected(std::make_error_code(std::errc::timed_out));
        return {};
    }

    void disconnect() {
        WiFi.disconnect(true, false);
        WiFi.mode(WIFI_OFF);
    }

} // namespace net
