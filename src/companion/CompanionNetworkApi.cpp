#include "companion/CompanionApi.h"

#include <utility>

#include "settings/SettingsRules.h"
#include "text/AsciiText.h"

namespace {

    namespace api = companion::api;

    [[nodiscard]] api::Result<api::NetworkUpdate> validateNetworkUpdate(api::NetworkUpdate update) {
        if (!update.ssid) {
            return std::unexpected(api::httpError(HTTP_CODE_UNPROCESSABLE_ENTITY, "invalid_network",
                                                  "Missing Wi-Fi SSID", "ssid"));
        }

        update.ssid = std::string{AsciiText::trim(*update.ssid)};
        if (update.ssid->empty()) {
            return std::unexpected(api::httpError(HTTP_CODE_UNPROCESSABLE_ENTITY, "invalid_network",
                                                  "Wi-Fi SSID is required", "ssid"));
        }
        if (update.ssid->size() > settings::rules::kWifiSsidMaxLength) {
            return std::unexpected(api::httpError(HTTP_CODE_UNPROCESSABLE_ENTITY, "invalid_network",
                                                  "Wi-Fi SSID is too long", "ssid"));
        }
        if (update.password && update.password->size() > settings::rules::kWifiPasswordMaxLength) {
            return std::unexpected(api::httpError(HTTP_CODE_UNPROCESSABLE_ENTITY, "invalid_network",
                                                  "Wi-Fi password is too long", "password"));
        }
        return update;
    }

} // namespace

void CompanionApi::storeNetwork(std::string ssid, std::string password) {
    settingsStore_.settings().network.ssid = std::move(ssid);
    settingsStore_.acceptChanges();
    settingsStore_.secrets().wifiPassword = std::move(password);
    settingsStore_.acceptSecretChanges();
    networkScreen_.begin(settingsStore_);
    networkScreen_.startupCheckPending = false;
}

companion::api::Result<const settings::NetworkSettings*> CompanionApi::getNetwork(httpd_req_t& request) {
    (void) request;
    return &settingsStore_.settings().network;
}

companion::api::Result<> CompanionApi::putNetwork(httpd_req_t& request) {
    return readJson<companion::api::NetworkUpdate>(request, 512, "Wi-Fi payload exceeds 512 bytes")
        .and_then(validateNetworkUpdate)
        .transform([this](companion::api::NetworkUpdate update) {
            storeNetwork(std::move(*update.ssid), update.password.value_or(""));
        });
}

companion::api::Result<> CompanionApi::deleteNetwork(httpd_req_t& request) {
    (void) request;
    storeNetwork({}, {});
    return {};
}
