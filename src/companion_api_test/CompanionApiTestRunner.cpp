#include <Arduino.h>
#include <esp_log.h>

#include "board/Board.h"
#include "board/BoardDisplay.h"
#include "board/BoardStorage.h"
#include "companion/http/CompanionApi.h"
#include "localization/LocaleCatalog.h"
#include "logging/Logger.h"
#include "settings/NvsSecurity.h"
#include "settings/SettingsStore.h"
#include "library/StorageManager.h"
#include "ui/Localization.h"
#include "ui/Ui.h"
#include "ui/screens/LibraryScreen.h"
#include "ui/screens/ReaderScreen.h"
#include "ui/screens/Screens.h"

namespace {

    settings::SettingsStore gSettings;
    locales::Catalog gLocales;
    ui::Context gUi{Board::Display::gfx()};
    screens::ReaderScreen gReader{Board::Display::gfx(), gSettings.settings().reading};
    screens::InterfaceScreen gInterface;
    screens::NetworkScreen gNetwork;
    screens::LibraryScreen gLibrary;
    screens::FocusScreen gFocus;
    StorageManager gStorage;
    CompanionApi gApi{gSettings, gStorage, gLocales, gUi, gReader, gInterface, gNetwork, gLibrary, gFocus};
    uint32_t gLastReadyLogMs = 0;
    uint32_t gFallbackReadyMs = 0;

    void validateInstalledFonts() {
        const auto families = gReader.fonts.families();
        for (size_t index = 1; index < families.size(); ++index) {
            const auto face = gReader.fonts.loadFace(index, 0);
            const bool rasterLoaded = face.raster.get().name == families[index].label;
            const bool shapingLoaded = !families[index].shaping || face.shaper != nullptr;
            if (!rasterLoaded || !shapingLoaded) {
                ESP_LOGE("api-test", "font unusable id=%s raster=%d shaping=%d", families[index].id.c_str(),
                         rasterLoaded, shapingLoaded);
            } else {
                ESP_LOGI("api-test", "font usable id=%s", families[index].id.c_str());
            }
        }
        gReader.fonts.clearLoaded();
    }

    void beginApiTest() {
        if (!Board::Display::begin())
            ESP_LOGE("api-test", "display init failed");
        gUi.setOrientation(Board::Display::defaultUiOrientation());

        gStorage.begin();
        fs::FS* filesystem = gStorage.mounted() ? &Board::Storage::filesystem() : nullptr;
        if (auto result = gSettings.begin(filesystem); !result)
            ESP_LOGW("api-test", "settings warning: %s", result.error().message.c_str());

        if (filesystem != nullptr)
            gLocales = locales::scanInstalled(*filesystem, static_cast<size_t>(UiText::Count));
        gUi.setLanguageCatalog(filesystem, &gLocales, &locales::loadUiFont);
        gReader.fonts.loadFromSd();
        validateInstalledFonts();
        auto& settings = gSettings.settings();
        if (!gReader.fonts.find(settings.reading.typography.fontId))
            settings.reading.typography.fontId = settings::TypographySettings{}.fontId;
        if (settings.interface.locale != Localization::kDefaultLocale
            && !locales::findPackForLocale(gLocales, settings.interface.locale))
            settings.interface.locale = Localization::kDefaultLocale;

        gInterface.begin(gUi, settings.interface, gLocales, &Board::Display::setBrightness);
        gReader.begin(gInterface.themes.resolve(settings.interface.selectedThemeId));
        gNetwork.begin(gSettings);
        gNetwork.startupCheckPending = false;
        if (filesystem != nullptr)
            gFocus.begin(*filesystem);
        else
            gFocus.begin();

        if (!gApi.begin()) {
            ESP_LOGE("api-test", "companion API failed to start");
            screens::status(gUi, "Companion API test", "Startup failed");
            return;
        }
        gFallbackReadyMs = millis() + 15000;
        screens::status(gUi, "Companion API test", gApi.statusLine1(), gApi.statusLine2());
    }

    void updateApiTest(uint32_t nowMs) {
        gSettings.update(nowMs);
        if (!gApi.active()) {
            if (nowMs - gLastReadyLogMs >= 2000) {
                gLastReadyLogMs = nowMs;
                ESP_LOGE("api-test", "not ready: %s %s", std::string{gApi.statusLine1()}.c_str(),
                         std::string{gApi.statusLine2()}.c_str());
            }
            delay(100);
            return;
        }
        const std::string& stationSsid = gSettings.settings().network.ssid;
        const bool stationReady = !stationSsid.empty() && gApi.statusLine1() == stationSsid;
        const bool accessPointReady = stationSsid.empty() || static_cast<int32_t>(nowMs - gFallbackReadyMs) >= 0;
        if ((stationReady || accessPointReady) && nowMs - gLastReadyLogMs >= 2000) {
            gLastReadyLogMs = nowMs;
            ESP_LOGI("api-test", "ready url=%s", std::string{gApi.statusLine2()}.c_str());
        }
        delay(1);
    }

} // namespace

void setup() {
    Serial.begin(115200);
    Logger::begin();
    delay(50);
    Board::System::begin();
    const uint32_t serialWaitStart = millis();
    while (!Serial && millis() - serialWaitStart < 2000)
        delay(10);
    Board::System::logStartupDiagnostics();
    if (!settings::initializeNvsEncryption()) {
        ESP_LOGE("main", "encrypted NVS initialization failed; restarting");
        delay(1000);
        ESP.restart();
        return;
    }
    ESP_LOGI("main", "companion API test setup");
    beginApiTest();
}

void loop() {
    updateApiTest(millis());
}
