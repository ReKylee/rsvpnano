#include "app/App.h"

#include <esp_log.h>
#include <utility>
#include "board/BoardStorage.h"

void App::applySettings() {
    loadAppearanceSettings();
    readerScreen_.applyTheme(interfaceScreen_.themes.resolve(settingsStore_.settings().interface.selectedThemeId));
    requestTypographyRefresh();
    networkScreen_.begin(settingsStore_);
    networkScreen_.startupCheckPending = false;
}

void App::loadAppearanceSettings() {
    auto& current = settingsStore_.settings();
    bool corrected = false;
    immediateUi_.setOrientation(current.reading.leftHanded ? Board::Display::rotatedUiOrientation()
                                                           : Board::Display::defaultUiOrientation());
    immediateUi_.setLanguageCatalog(storage_.mounted() ? &Board::Storage::filesystem() : nullptr, &localeCatalog_,
                                    &locales::loadUiFont);
    if (!readerScreen_.fonts.find(current.reading.typography.fontId)) {
        current.reading.typography.fontId = settings::TypographySettings{}.fontId;
        corrected = true;
    }
    if (current.interface.locale != Localization::kDefaultLocale
        && !locales::findPackForLocale(localeCatalog_, current.interface.locale)) {
        current.interface.locale = Localization::kDefaultLocale;
        corrected = true;
    }

    reloadUiAssets();
    corrected |=
        interfaceScreen_.begin(immediateUi_, current.interface, localeCatalog_, &Board::Display::setBrightness);
    if (corrected)
        settingsStore_.acceptChanges();
}

void App::reloadUiAssets() {
    locales::UiAssets assets;
    if (storage_.mounted()) {
        auto loaded =
            locales::loadUiAssets(Board::Storage::filesystem(), localeCatalog_,
                                  settingsStore_.settings().interface.locale, static_cast<size_t>(UiText::Count));
        if (loaded)
            assets = std::move(*loaded);
        else
            ESP_LOGW("languages", "selected UI pack rejected: %s", loaded.error().c_str());
    }
    immediateUi_.setLanguageAssets(std::move(assets));
}
