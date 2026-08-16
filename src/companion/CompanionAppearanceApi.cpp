#include "companion/CompanionApi.h"

#include <string>
#include <utility>

#include "board/BoardStorage.h"
#include "text/AsciiText.h"
#include "text/LocaleTag.h"
#include "ui/Localization.h"

namespace api = companion::api;

api::Result<> CompanionApi::putThemeSelection(httpd_req_t& request) {
    return readSelectionId(request).and_then([this](std::string id) -> api::Result<> {
        auto& themes = interfaceScreen_.themes;
        themes.loadFromSd();

        if (!themes.selectById(id)) {
            return std::unexpected(api::httpError(HTTP_CODE_NOT_FOUND, "theme_not_found",
                                                  "Theme is not installed", "id"));
        }

        const std::string selectedId = themes.selected().id;
        if (settingsStore_.settings().interface.selectedThemeId != selectedId) {
            settings::DeviceSettings next = settingsStore_.settings();
            next.interface.selectedThemeId = selectedId;
            settingsStore_.replace(std::move(next), settings::SettingsSource::Companion);
        }

        const auto& theme = themes.selected();
        ui_.setTheme(theme);
        readerScreen_.applyTheme(theme);
        return {};
    });
}

api::Result<> CompanionApi::putFontSelection(httpd_req_t& request) {
    return readSelectionId(request).and_then([this](std::string id) -> api::Result<> {
        const auto family = readerScreen_.fonts.find(id);
        if (!family) {
            return std::unexpected(api::httpError(HTTP_CODE_NOT_FOUND, "font_not_found", "Font is not installed",
                                                  "id"));
        }

        const std::string selectedId = family->get().id;
        if (settingsStore_.settings().reading.typography.fontId != selectedId) {
            settings::DeviceSettings next = settingsStore_.settings();
            next.reading.typography.fontId = selectedId;
            settingsStore_.replace(std::move(next), settings::SettingsSource::Companion);
        }

        readerScreen_.refreshTypography(settingsStore_.settings().reading, readerScreen_.session.state.overrides);
        return {};
    });
}

api::Result<> CompanionApi::putLocaleSelection(httpd_req_t& request) {
    return readSelectionId(request).and_then([this](std::string id) -> api::Result<> {
        auto locale = LocaleTag::normalize(id);
        if (!locale
            || (*locale != Localization::kDefaultLocale && !locales::findPackForLocale(localeCatalog_, *locale))) {
            return std::unexpected(api::httpError(HTTP_CODE_NOT_FOUND, "locale_not_found",
                                                  "Interface locale is not installed", "id"));
        }

        auto assets = locales::loadUiAssets(Board::Storage::filesystem(), localeCatalog_, *locale,
                                            static_cast<size_t>(UiText::Count));
        if (!assets) {
            return std::unexpected(api::httpError(HTTP_CODE_UNPROCESSABLE_ENTITY, "invalid_locale_assets",
                                                  std::move(assets.error())));
        }

        if (settingsStore_.settings().interface.locale != *locale) {
            settings::DeviceSettings next = settingsStore_.settings();
            next.interface.locale = *locale;
            settingsStore_.replace(std::move(next), settings::SettingsSource::Companion);
        }

        ui_.setLanguageAssets(std::move(*assets));
        ui_.setLocale(settingsStore_.settings().interface.locale);
        return {};
    });
}

api::Result<std::string> CompanionApi::readSelectionId(httpd_req_t& request) {
    return readJson<api::AppearanceSelection>(request, 256, "Appearance selection exceeds 256 bytes")
        .and_then([](api::AppearanceSelection selection) -> api::Result<std::string> {
            selection.id = std::string{AsciiText::trim(selection.id)};
            if (selection.id.empty()) {
                return std::unexpected(api::httpError(HTTP_CODE_BAD_REQUEST, "missing_field",
                                                      "Selection id is required", "id"));
            }
            return std::move(selection.id);
        });
}
