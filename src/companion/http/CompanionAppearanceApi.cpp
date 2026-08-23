#include "companion/http/CompanionApi.h"

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
        const ui::themes::Theme* theme = themes.find(id);
        if (theme == nullptr) {
            return std::unexpected(api::httpError(HTTP_CODE_NOT_FOUND, "theme_not_found", "Theme is not installed",
                                                  "id"));
        }

        if (settingsStore_.settings().interface.selectedThemeId != theme->id) {
            settingsStore_.settings().interface.selectedThemeId = theme->id;
            settingsStore_.acceptChanges();
        }

        ui_.setTheme(*theme);
        readerScreen_.applyTheme(*theme);
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

        const std::string selectedId = family->id;
        if (settingsStore_.settings().reading.typography.fontId != selectedId) {
            settingsStore_.settings().reading.typography.fontId = selectedId;
            settingsStore_.acceptChanges();
        }

        readerScreen_.releaseRuntimeCaches();
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
            settingsStore_.settings().interface.locale = *locale;
            settingsStore_.acceptChanges();
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
