#pragma once

#include <Arduino.h>

#include <functional>

namespace net {

    // Reports association progress as a percentage in [0, 100] while connecting.
    using WifiProgress = std::function<void(int percent)>;

    // Brings up WIFI_STA and blocks until associated or the connect timeout
    // elapses. Returns true only when connected. progress may be null.
    bool connectStation(const char* ssid, const char* password, const WifiProgress& progress = nullptr,
                        uint32_t timeoutMs = 15000);

    // Disconnects and powers the radio off (WIFI_OFF).
    void disconnect();

} // namespace net
