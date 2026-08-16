#include "companion/CompanionApi.h"

#include <utility>

#include "board/BoardDisplay.h"

namespace api = companion::api;

api::Result<api::SettingsResponse> CompanionApi::getSettings(httpd_req_t& request) {
    (void) request;
    const auto& settings = settingsStore_.settings();
    return api::SettingsResponse{settings.reading, settings.interface, settings.updates};
}

api::Result<> CompanionApi::patchReadingSettings(httpd_req_t& request) {
    return readJson(request, settings::kMaxSettingsBytes, "Settings payload exceeds 8 KB",
                    settingsStore_.settings().reading)
        .transform([this](settings::ReadingSettings reading) {
            settings::DeviceSettings next = settingsStore_.settings();
            reading.typography.fontId = next.reading.typography.fontId;
            next.reading = std::move(reading);
            settingsStore_.replace(std::move(next), settings::SettingsSource::Companion);
            readerScreen_.refreshTypography(settingsStore_.settings().reading, readerScreen_.session.state.overrides);
        });
}

api::Result<> CompanionApi::patchDisplaySettings(httpd_req_t& request) {
    return readJson(request, settings::kMaxSettingsBytes, "Display settings payload exceeds 8 KB",
                    settingsStore_.settings().interface)
        .transform([this](settings::InterfaceSettings interface) {
            settings::DeviceSettings next = settingsStore_.settings();
            interface.locale = next.interface.locale;
            interface.selectedThemeId = next.interface.selectedThemeId;
            next.interface = std::move(interface);
            settingsStore_.replace(std::move(next), settings::SettingsSource::Companion);
            Board::Display::setBrightness(settingsStore_.settings().interface.brightnessPercent);
        });
}

api::Result<> CompanionApi::patchUpdateSettings(httpd_req_t& request) {
    return readJson(request, settings::kMaxSettingsBytes, "Update settings payload exceeds 8 KB",
                    settingsStore_.settings().updates)
        .transform([this](settings::UpdateSettings updates) {
            settings::DeviceSettings next = settingsStore_.settings();
            next.updates = std::move(updates);
            settingsStore_.replace(std::move(next), settings::SettingsSource::Companion);
            networkScreen_.begin(settingsStore_);
            networkScreen_.startupCheckPending = false;
        });
}
