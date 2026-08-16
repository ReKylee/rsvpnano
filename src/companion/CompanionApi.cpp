#include "companion/CompanionApi.h"

#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_log.h>

#include <algorithm>
#include <cstdio>
#include <expected>
#include <string>
#include <utility>

#include "board/BoardStorage.h"
#include "text/AsciiText.h"
#include "update/OtaUpdater.h"

namespace {

    struct StartupFailure {
        std::string status;
        std::string detail;
    };

    [[nodiscard]] std::string httpUrl(const IPAddress& address) {
        return std::string{"http://"} + address.toString().c_str();
    }

} // namespace

bool CompanionApi::begin() {
    if (active())
        return true;

    statusLine1_ = "Starting sync";
    statusLine2_ = "Preparing Wi-Fi";

    auto startup = startAccessPoint()
                       .transform_error([](std::string detail) {
                           return StartupFailure{"Wi-Fi failed", std::move(detail)};
                       })
                       .and_then([this] {
                           return startServer().transform_error([](std::string detail) {
                               return StartupFailure{"Server failed", std::move(detail)};
                           });
                       });
    if (!startup) {
        StartupFailure failure = std::move(startup.error());
        end();
        statusLine1_ = std::move(failure.status);
        statusLine2_ = std::move(failure.detail);
        return false;
    }

    statusLine1_ = accessPointSsid_;
    statusLine2_ = httpUrl(WiFi.softAPIP());
    ESP_LOGI("companion", "ready ssid=%s url=%s", accessPointSsid_.c_str(), statusLine2_.c_str());

    if (auto station = startStation(); !station) {
        ESP_LOGW("companion", "station unavailable; direct connection remains active: %s",
                 station.error().c_str());
    }
    return true;
}

bool CompanionApi::update() {
    if (!active())
        return false;

    const bool connected = WiFi.status() == WL_CONNECTED;
    if (connected == stationConnected_)
        return false;

    stationConnected_ = connected;
    if (!connected) {
        stopMdns();
        statusLine1_ = accessPointSsid_;
        statusLine2_ = httpUrl(WiFi.softAPIP());
        ESP_LOGW("companion", "station disconnected; direct connection remains available at %s",
                 statusLine2_.c_str());
        return true;
    }

    const std::string& ssid = settingsStore_.settings().network.wifiSsid;
    statusLine1_ = ssid;
    statusLine2_ = httpUrl(WiFi.localIP());
    if (auto mdns = startMdns(); !mdns) {
        ESP_LOGW("companion", "station connected without mDNS discovery: %s", mdns.error().c_str());
    }
    ESP_LOGI("companion", "station ready ssid=%s ip=%s fallback=%s", ssid.c_str(),
             WiFi.localIP().toString().c_str(), accessPointSsid_.c_str());
    return true;
}

void CompanionApi::end() {
    stopMdns();
    stopServer();

    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, false);
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);

    stationConnected_ = false;
    accessPointSsid_.clear();
    statusLine1_ = "Idle";
    statusLine2_.clear();
}

bool CompanionApi::active() const {
    return server_ != nullptr;
}

std::string_view CompanionApi::statusLine1() const {
    return statusLine1_;
}

std::string_view CompanionApi::statusLine2() const {
    return statusLine2_;
}

CompanionApi::OperationResult CompanionApi::startAccessPoint() {
    accessPointSsid_ = "RSVP-Nano-" + deviceSuffix();

    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    if (!WiFi.mode(WIFI_AP_STA))
        return std::unexpected("Could not enable AP+STA mode");

    const IPAddress address{192, 168, 4, 1};
    const IPAddress subnet{255, 255, 255, 0};
    if (!WiFi.softAPConfig(address, address, subnet))
        return std::unexpected("Could not configure the direct connection address");

    if (!WiFi.softAP(accessPointSsid_.c_str()))
        return std::unexpected("Could not start the direct connection access point");

    ESP_LOGI("companion", "softAP ssid=%s ip=%s", accessPointSsid_.c_str(),
             WiFi.softAPIP().toString().c_str());
    return {};
}

CompanionApi::OperationResult CompanionApi::startStation() {
    const std::string& ssid = settingsStore_.settings().network.wifiSsid;
    if (ssid.empty())
        return {};

    if (WiFi.begin(ssid.c_str(), settingsStore_.secrets().wifiPassword.c_str()) == WL_CONNECT_FAILED)
        return std::unexpected("Could not start the saved Wi-Fi connection");

    ESP_LOGI("companion", "station connecting ssid=%s; softAP remains available", ssid.c_str());
    return {};
}

CompanionApi::OperationResult CompanionApi::startMdns() {
    if (mdnsStarted_)
        return {};

    const std::string suffix = deviceSuffix();
    std::string hostname = "rsvp-nano-" + suffix;
    const std::string instanceName = "RSVP-Nano-" + suffix;
    std::ranges::transform(hostname, hostname.begin(), AsciiText::toLower);

    if (!MDNS.begin(hostname.c_str()))
        return std::unexpected("Could not start the mDNS responder");

    MDNS.setInstanceName(instanceName.c_str());
    if (!MDNS.addService("rsvpnano", "tcp", 80)) {
        MDNS.end();
        return std::unexpected("Could not advertise the companion service");
    }

    MDNS.addServiceTxt("rsvpnano", "tcp", "id", suffix.c_str());
    MDNS.addServiceTxt("rsvpnano", "tcp", "api", "2");
    mdnsStarted_ = true;
    return {};
}

void CompanionApi::stopMdns() {
    if (!mdnsStarted_)
        return;
    MDNS.end();
    mdnsStarted_ = false;
}

companion::api::Result<companion::api::DeviceInfo> CompanionApi::getDevice(httpd_req_t& request) {
    (void) request;
    return companion::api::DeviceInfo{
        .firmwareVersion = std::string{OtaUpdater::currentVersion()},
        .otaAsset = Board::Config::OTA_ASSET_NAME,
    };
}

std::string CompanionApi::deviceSuffix() const {
    const uint64_t mac = ESP.getEfuseMac();
    char suffix[7]{};
    std::snprintf(suffix, sizeof(suffix), "%06X", static_cast<unsigned int>(mac & 0xFFFFFF));
    return suffix;
}
