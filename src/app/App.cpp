#include "app/App.h"

#include <esp_log.h>
#include "app/screens/status/StatusScreen.h"
#include "app/screens/standby/StandbyTiming.h"
#include "board/BoardAudio.h"
#include "board/BoardInput.h"
#include "board/BoardStorage.h"
#include "logging/Logger.h"
#include "settings/NvsSecurity.h"
#include "library/ReadingProgress.h"

namespace {
    constexpr uint32_t kBootSplashMs = 650;
    bool deadlineReached(uint32_t nowMs, uint32_t deadlineMs) {
        return static_cast<int32_t>(nowMs - deadlineMs) >= 0;
    }
} // namespace

void App::begin() {
    prefs_.begin(settings::kStateNvsNamespace);
    storage_.setStatusCallback(&App::renderStorageStatus, this);
    bootMs_ = millis();
    lastActivityMs_ = bootMs_;
    statusUntilMs_ = bootMs_ + kBootSplashMs;

    if (!Board::Display::begin()) {
        ESP_LOGE("app", "display init failed");
    }
    immediateUi_.setOrientation(Board::Display::defaultUiOrientation());
    immediateUi_.setTheme(interfaceScreen_.themes.resolve(settingsStore_.settings().interface.selectedThemeId));
    screens::status(immediateUi_, immediateUi_.text(UiText::Ready));
    if (!Input::begin())
        ESP_LOGE("input", "startup failed");
    immediateUi_.setTouchSource({.surface = Board::Input::touchSurface(), .poll = &Input::pollTouch});

    storage_.begin();
    Logger::startupCheckpoint("storage");
    fs::FS* filesystem = storage_.mounted() ? &Board::Storage::filesystem() : nullptr;
    if (auto result = settingsStore_.begin(filesystem); !result)
        ESP_LOGW("settings", "startup warning: %s", result.error().message.c_str());
    Logger::startupCheckpoint("settings");
    Logger::checkpoint("locale_catalog");
    localeCatalog_ = filesystem == nullptr
                       ? locales::Catalog{}
                       : locales::scanInstalled(*filesystem, static_cast<size_t>(UiText::Count));
    ESP_LOGI("languages", "catalog ready installed=%u", static_cast<unsigned>(localeCatalog_.size()));
    Logger::startupCheckpoint("locale_catalog");
    readerScreen_.fonts.loadFromSd();
    Logger::startupCheckpoint("fonts");
    Logger::checkpoint("ui_locale");
    loadAppearanceSettings();
    Logger::startupCheckpoint("locales");
    Board::Power::updateBattery(battery_, bootMs_, true);
    readerScreen_.begin(interfaceScreen_.themes.resolve(settingsStore_.settings().interface.selectedThemeId));
    networkScreen_.begin(settingsStore_);
    if (storage_.mounted())
        focusScreen_.begin(Board::Storage::filesystem());
    else
        focusScreen_.begin();
    readerScreen_.loadInitialBook(immediateUi_, storage_, prefs_, bootMs_);
    Logger::startupCheckpoint("book");
    libraryScreen_.invalidate();
    ESP_LOGI("startup", "ready");
}

void App::update(uint32_t nowMs) {
    serialCompanion_.update(nowMs);
    Input::ActionMask actions;
    while (Input::poll(actions)) {
        lastActivityMs_ = nowMs;
        handleInput(actions, nowMs);
        nowMs = millis();
    }
    while (immediateUi_.pollTouch(nowMs)) {
        lastActivityMs_ = nowMs;
        handleTouch(nowMs);
        nowMs = millis();
    }

    updateBackgroundJob();
    const bool preparingTypography = typographyJobActive();

    if (backgroundJobActive() && !preparingTypography)
        return;

    if (screen_ == screens::Screen::Status) {
        if (statusUntilMs_ == 0 || !deadlineReached(nowMs, statusUntilMs_))
            return;
        statusUntilMs_ = 0;
        if (restartAfterStatus_) {
            ESP.restart();
            return;
        }
        screen_ = statusDestination_;
        if (networkScreen_.startupCheckPending) {
            networkScreen_.startupCheckPending = false;
            runOtaCheck(false);
            return;
        }
    }

    if (screen_ == screens::Screen::Standby) {
        if (nowMs - standbyEnteredMs_ >= screens::kStandbyPowerOffMs) {
            powerOff(nowMs);
            return;
        }
        standbyScreen_.update(immediateUi_, nowMs);
        return;
    }

    if (companionApi_.active() || serialCompanion_.active()) {
        companionApi_.renderStatus(serialCompanion_.active());
        settingsStore_.update(nowMs);
        Board::Power::updateBattery(battery_, nowMs);
        return;
    }

    if (!usbTransfer_.active())
        settingsStore_.update(nowMs);

    Board::Power::updateBattery(battery_, nowMs);
    if (!preparingTypography)
        readerScreen_.update(prefs_, nowMs);
    if (screen_ == screens::Screen::FocusSession) {
        if (focusScreen_.update(nowMs))
            Board::Audio::beep();
    }
    ReadingProgress::save(readerScreen_.session, prefs_, false, nowMs);

    renderScreen(nowMs);
    if (!preparingTypography && !readerScreen_.session.playing && !companionApi_.active() && !usbTransfer_.active()
        && screen_ != screens::Screen::FocusSession && screen_ != screens::Screen::Status
        && screens::kStandbyDurationsMs[settingsStore_.settings().interface.standbyTimerIndex] > 0
        && nowMs - lastActivityMs_ >= screens::kStandbyDurationsMs[settingsStore_.settings().interface.standbyTimerIndex]) {
        enterStandby(nowMs);
    }
}
