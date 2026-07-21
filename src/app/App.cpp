#include "app/App.h"
#include <esp_log.h>

#include <array>
#include <esp_system.h>
#include <string>

#include "board/BoardAudio.h"
#include "board/BoardConfig.h"
#include "board/BoardInput.h"
#include "board/BoardPower.h"
#include "board/BoardStorage.h"
#include "board/BoardSystem.h"
#include "rss/RssFeeds.h"
#include "settings/NvsSecurity.h"
#include "storage/index/ReadingProgress.h"
#include "update/OtaUpdater.h"

namespace {

    constexpr uint32_t kBootSplashMs = 650;
    constexpr std::array<uint32_t, 5> kStandbyMs = {
        0, 1UL * 60UL * 1000UL, 5UL * 60UL * 1000UL, 15UL * 60UL * 1000UL, 30UL * 60UL * 1000UL,
    };
    constexpr uint32_t kStandbyPowerOffMs = 5UL * 60UL * 1000UL;
    void powerOffBoard() {
        if (!Board::Power::powerOff())
            ESP_LOGI("app", "hardware power off unavailable; entering light sleep");
        delay(1200);
        Board::System::lightSleep(0);
        esp_restart();
    }
} // namespace

void App::begin() {
    prefs_.begin(settings::kStateNvsNamespace);
    storage_.setStatusCallback(&App::renderStorageStatus, this);
    Input::begin();
    bootMs_ = millis();
    lastActivityMs_ = bootMs_;
    immediateUi_.setTouchSource({Board::Input::touchSurface(), Board::Input::touchTiming(), &Board::Input::beginTouch,
                                 &Board::Input::touchReady, &Board::Input::readTouch},
                                bootMs_);

    if (!Board::Display::begin()) {
        ESP_LOGE("app", "display init failed");
    }
    immediateUi_.setOrientation(Board::Display::defaultUiOrientation());
    immediateUi_.setTheme(interfaceScreen_.themes.selected());
    screens::status(immediateUi_, immediateUi_.text(UiText::Ready));

    storage_.begin();
    if (auto result = settingsStore_.begin(storage_.mounted() ? &Board::Storage::filesystem() : nullptr); !result)
        ESP_LOGW("settings", "startup warning: %s", result.error().message.c_str());
    migrateLegacyStorage();
    readerScreen_.fonts.loadFromSd();
    auto& deviceSettings = settingsStore_.settings();
    interfaceScreen_.begin(immediateUi_, deviceSettings.interface, deviceSettings.reading.typography,
                           readerScreen_.fonts, &Board::Display::setBrightness);
    readerScreen_.begin(deviceSettings.reading, interfaceScreen_.themes.selected(), bootMs_);
    networkScreen_.begin(settingsStore_);
    focusScreen_.begin(storage_.mounted() ? &Board::Storage::filesystem() : nullptr);
    readerScreen_.loadInitialBook(immediateUi_, storage_, prefs_, bootMs_);
    libraryScreen_.invalidate();
}

void App::update(uint32_t nowMs) {
    Input::Event event;
    while (Input::poll(event, nowMs)) {
        lastActivityMs_ = nowMs;
        handleInput(event, nowMs);
        nowMs = millis();
    }
    if (immediateUi_.pollTouch(nowMs)) {
        lastActivityMs_ = nowMs;
        handleTouch(nowMs);
        nowMs = millis();
    }

    if (!usbTransfer_.active())
        settingsStore_.update(nowMs);

    if (screen_ == screens::Screen::Standby) {
        if (nowMs - standbyEnteredMs_ >= kStandbyPowerOffMs) {
            powerOff(nowMs);
            return;
        }
        standbyScreen_.update(immediateUi_, nowMs);
        return;
    }

    if (screen_ == screens::Screen::Status && nowMs - bootMs_ >= kBootSplashMs) {
        screen_ = screens::Screen::Reader;
        if (networkScreen_.startupCheckPending) {
            networkScreen_.startupCheckPending = false;
            runOtaCheck(false);
            return;
        }
    }

    readerScreen_.battery.update(nowMs);
    readerScreen_.update(prefs_, nowMs);
    if (sync_.active() && sync_.update())
        reloadSettings(nowMs);
    if (screen_ == screens::Screen::FocusSession) {
        if (focusScreen_.update(nowMs))
            Board::Audio::beep();
    }
    readerScreen_.book.save(prefs_, readerScreen_.reader, false, nowMs);

    renderScreen(nowMs);
    if (!readerScreen_.reader.playing() && !sync_.active() && !usbTransfer_.active()
        && screen_ != screens::Screen::FocusSession && screen_ != screens::Screen::Status
        && kStandbyMs[settingsStore_.settings().interface.standbyTimerIndex] > 0
        && nowMs - lastActivityMs_ >= kStandbyMs[settingsStore_.settings().interface.standbyTimerIndex]) {
        enterStandby(nowMs);
    }
}

void App::renderScreen(uint32_t nowMs) {
    const screens::Screen renderedScreen = screen_;
    screens::Action action = screens::Action::None;
    switch (screen_) {
    case screens::Screen::Status:
        screens::status(immediateUi_, immediateUi_.text(UiText::Ready));
        return;
    case screens::Screen::Reader:
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        readerScreen_.draw(immediateUi_, storage_, nowMs);
        immediateUi_.endFrame();
        return;
    case screens::Screen::Library: {
        const auto& items =
            libraryScreen_.items(storage_, readerScreen_.store, readerScreen_.reader, readerScreen_.book);
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        const screens::LibraryResult result = libraryScreen_.draw(immediateUi_, items, nowMs, screen_);
        immediateUi_.endFrame();
        if (result.open) {
            const size_t bookIndex = libraryScreen_.selectedIndex();
            if (bookIndex < storage_.bookCount()
                && readerScreen_.openBook(immediateUi_, storage_, prefs_, bookIndex, nowMs)) {
                readerScreen_.reader.pause();
                screen_ = screens::Screen::Reader;
                renderScreen(nowMs);
            }
        } else {
            handleScreenAction(result.action, nowMs);
        }
        return;
    }
    case screens::Screen::Usb:
        screens::status(immediateUi_, "USB", usbTransfer_.statusMessage(), immediateUi_.text(UiText::HoldPowerToExit));
        return;
    case screens::Screen::Standby:
        standbyScreen_.draw(immediateUi_);
        return;
    case screens::Screen::Read:
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        {
            action = screens::read(immediateUi_,
                                   {readerScreen_.book.title(storage_), readerScreen_.book.metadata.author,
                                    ReadingProgress::percent(readerScreen_.reader.currentIndex(),
                                                             readerScreen_.reader.wordCount())},
                                   screen_);
        }
        break;
    case screens::Screen::Chapters:
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        action = chaptersScreen_.draw(immediateUi_, readerScreen_.book.metadata.chapters, readerScreen_.reader,
                                      settingsStore_.settings().reading, nowMs, screen_);
        break;
    case screens::Screen::Settings:
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        action = screens::settings(immediateUi_, screen_);
        break;
    case screens::Screen::ReadingSettings: {
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        if (screens::readingSettings(immediateUi_, readerScreen_.reader, settingsStore_.settings().reading, screen_))
            settingsStore_.acceptChanges();
        break;
    }
    case screens::Screen::InterfaceSettings: {
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        if (interfaceScreen_.draw(immediateUi_, settingsStore_.settings().interface, kStandbyMs,
                                  &Board::Display::setBrightness, screen_)) {
            settingsStore_.acceptChanges();
            readerScreen_.applyTheme(interfaceScreen_.themes.selected());
        }
        break;
    }
    case screens::Screen::PacingSettings: {
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        if (screens::pacingSettings(immediateUi_, readerScreen_.reader, settingsStore_.settings().reading.pacing,
                                    screen_))
            settingsStore_.acceptChanges();
        break;
    }
    case screens::Screen::TypographySettings: {
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        if (screens::typographySettings(immediateUi_, readerScreen_.book.state.bookTypographyOverride,
                                        interfaceScreen_.themes.selected().definition.typography, readerScreen_.fonts,
                                        screen_)) {
            readerScreen_.book.mirror(readerScreen_.store, readerScreen_.reader);
            readerScreen_.refreshTypography();
        }
        break;
    }
    case screens::Screen::ReaderSettings:
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        if (screens::readerSettings(immediateUi_, settingsStore_.settings().reading, screen_))
            settingsStore_.acceptChanges();
        break;
    case screens::Screen::NetworkSettings:
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        networkScreen_.draw(immediateUi_, settingsStore_, screen_);
        break;
    case screens::Screen::WifiScan:
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        networkScreen_.drawWifiScan(immediateUi_, settingsStore_, screen_);
        break;
    case screens::Screen::WifiConnect:
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        if (networkScreen_.drawWifiConnect(immediateUi_, settingsStore_, screen_))
            return;
        break;
    case screens::Screen::NetworkEdit:
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        networkScreen_.drawEdit(immediateUi_, settingsStore_, screen_);
        break;
    case screens::Screen::Device:
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        action = screens::device(immediateUi_, storage_.mounted(), storage_.bookCount(), settings::nvsEncryptionState(),
                                 screen_);
        break;
    case screens::Screen::StorageEncryption:
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        action = screens::storageEncryption(immediateUi_, settings::nvsEncryptionState(), screen_);
        break;
    case screens::Screen::Sync:
        if (sync_.active()) {
            screens::status(immediateUi_, immediateUi_.text(UiText::Sync), sync_.statusLine1(), sync_.statusLine2());
            return;
        }
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        action = screens::sync(immediateUi_, screen_);
        break;
    case screens::Screen::Ota: {
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        const String firmwareVersion = OtaUpdater{}.currentVersion();
        action = screens::ota(immediateUi_, firmwareVersion.c_str(), screen_);
        break;
    }
    case screens::Screen::FocusTimers:
    case screens::Screen::FocusEditor:
    case screens::Screen::FocusNameEdit:
    case screens::Screen::FocusSession: {
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        action = focusScreen_.draw(immediateUi_, nowMs, screen_);
        immediateUi_.endFrame();
        if (action != screens::Action::None) {
            handleScreenAction(action, nowMs);
            return;
        }
        if (renderedScreen == screens::Screen::FocusSession && screen_ != screens::Screen::FocusSession)
            focusScreen_.close();
        if (screen_ != renderedScreen)
            renderScreen(nowMs);
        return;
    }
    }
    immediateUi_.endFrame();
    if (screen_ != renderedScreen) {
        if (screen_ == screens::Screen::Library)
            libraryScreen_.reset();
    }
    handleScreenAction(action, nowMs);
}

void App::handleScreenAction(screens::Action action, uint32_t nowMs) {
    switch (action) {
    case screens::Action::None:
        return;
    case screens::Action::Resume:
        readerScreen_.reader.pause();
        screen_ = screens::Screen::Reader;
        renderScreen(nowMs);
        return;
    case screens::Action::PowerOff:
        powerOff(nowMs);
        return;
    case screens::Action::CompanionSync:
        immediateUi_.invalidate();
        screens::status(immediateUi_, immediateUi_.text(UiText::CompanionSync), immediateUi_.text(UiText::Connecting));
        sync_.begin();
        renderScreen(nowMs);
        return;
    case screens::Action::RssRefresh:
        runRss();
        return;
    case screens::Action::UsbTransfer:
        enterUsbTransfer(nowMs);
        return;
    case screens::Action::StorageStatus: {
        const std::string entries =
            std::to_string(storage_.bookCount()) + " " + std::string(immediateUi_.text(UiText::LibraryEntries));
        screens::status(immediateUi_, immediateUi_.text(UiText::Storage),
                        immediateUi_.text(storage_.mounted() ? UiText::SdReady : UiText::SdUnavailable), entries);
        delay(1200);
        renderScreen(nowMs);
        break;
    }
    case screens::Action::EnableStorageEncryption:
        screens::status(immediateUi_, immediateUi_.text(UiText::StorageEncryption),
                        immediateUi_.text(UiText::EnablingEncryption));
        readerScreen_.book.save(prefs_, readerScreen_.reader, true, nowMs);
        readerScreen_.book.mirror(readerScreen_.store, readerScreen_.reader);
        if (!storage_.mounted() || !settings::enableNvsEncryption(prefs_, settingsStore_)) {
            screens::status(immediateUi_, immediateUi_.text(UiText::StorageEncryption),
                            immediateUi_.text(UiText::Unavailable));
            delay(1200);
            screen_ = screens::Screen::Device;
            renderScreen(nowMs);
        }
        return;
    case screens::Action::OtaCheck:
        runOtaCheck(false);
        return;
    case screens::Action::OtaInstall:
        runOtaCheck(true);
        return;
    }
}

void App::handleInput(const Input::Event& event, uint32_t nowMs) {
    if (screen_ == screens::Screen::Standby) {
        exitStandby(nowMs);
        return;
    }
    if (usbTransfer_.active() && Input::hasAction(event.actions, Input::ActionPowerOff)) {
        exitUsbTransfer();
        return;
    }
    if (usbTransfer_.active()
        && (Input::hasAction(event.actions, Input::ActionBack)
            || Input::hasAction(event.actions, Input::ActionOpenMenu))) {
        exitUsbTransfer(screens::Screen::Read);
        return;
    }
    if (screen_ == screens::Screen::FocusSession
        && (Input::hasAction(event.actions, Input::ActionBack)
            || Input::hasAction(event.actions, Input::ActionSelect))) {
        focusScreen_.close();
        screen_ = screens::Screen::FocusTimers;
        renderScreen(nowMs);
        return;
    }
    if (Input::hasAction(event.actions, Input::ActionPowerOff)) {
        powerOff(nowMs);
        return;
    }
    if (Input::hasAction(event.actions, Input::ActionStandby)) {
        enterStandby(nowMs);
        return;
    }
    if (Input::hasAction(event.actions, Input::ActionOpenMenu) || Input::hasAction(event.actions, Input::ActionBack)) {
        if (sync_.active()) {
            sync_.end();
            reloadSettings(nowMs);
            screen_ = screens::Screen::Device;
            renderScreen(nowMs);
        } else if (screen_ != screens::Screen::Reader) {
            if (screen_ == screens::Screen::Read) {
                readerScreen_.reader.pause();
                screen_ = screens::Screen::Reader;
            } else if (screen_ == screens::Screen::StorageEncryption || screen_ == screens::Screen::Sync
                       || screen_ == screens::Screen::Ota) {
                screen_ = screens::Screen::Device;
            } else if (screen_ == screens::Screen::WifiConnect) {
                networkScreen_.closeWifi();
                screen_ = screens::Screen::WifiScan;
            } else if (screen_ == screens::Screen::WifiScan || screen_ == screens::Screen::NetworkEdit) {
                if (screen_ == screens::Screen::WifiScan)
                    networkScreen_.closeWifi();
                screen_ = screens::Screen::NetworkSettings;
            } else if (screen_ == screens::Screen::ReadingSettings || screen_ == screens::Screen::InterfaceSettings
                       || screen_ == screens::Screen::PacingSettings || screen_ == screens::Screen::TypographySettings
                       || screen_ == screens::Screen::ReaderSettings || screen_ == screens::Screen::NetworkSettings) {
                screen_ = screens::Screen::Settings;
            } else if (screen_ == screens::Screen::FocusNameEdit) {
                screen_ = screens::Screen::FocusEditor;
            } else if (screen_ == screens::Screen::FocusEditor) {
                screen_ = screens::Screen::FocusTimers;
            } else {
                screen_ = screens::Screen::Read;
            }
            renderScreen(nowMs);
        } else {
            readerScreen_.book.save(prefs_, readerScreen_.reader, true, nowMs);
            readerScreen_.reader.pause();
            libraryScreen_.invalidate();
            screen_ = screens::Screen::Read;
            renderScreen(nowMs);
        }
        return;
    }
    if (Input::hasAction(event.actions, Input::ActionSelect)
        || Input::hasAction(event.actions, Input::ActionPlayPause)) {
        if (screen_ == screens::Screen::Reader) {
            readerScreen_.toggle(prefs_, nowMs);
        }
    }
}

void App::handleTouch(uint32_t nowMs) {
    const ui::Touch* touch = immediateUi_.touch();
    if (touch == nullptr)
        return;
    if (screen_ == screens::Screen::Standby) {
        exitStandby(nowMs);
        return;
    }
    if (sync_.active() || usbTransfer_.active() || screen_ == screens::Screen::Status)
        return;
    if (screen_ == screens::Screen::Reader) {
        readerScreen_.handleTouch(immediateUi_, nowMs, prefs_, settingsStore_);
    } else {
        renderScreen(nowMs);
    }
}

void App::runRss() {
    readerScreen_.reader.pause();
    screens::status(immediateUi_, "RSS", immediateUi_.text(UiText::CheckingFeeds));
    const RssFeeds::Result result =
        RssFeeds::check(prefs_, settingsStore_.settings(), settingsStore_.secrets(), &App::renderStorageStatus, this);
    storage_.refreshBooks();
    libraryScreen_.invalidate();
    screens::status(immediateUi_, "RSS", result.summary.c_str(), result.detail.c_str());
    delay(1400);
    screen_ = screens::Screen::Reader;
    renderScreen(millis());
}

void App::enterUsbTransfer(uint32_t nowMs) {
#if RSVP_USB_TRANSFER_ENABLED
    readerScreen_.book.save(prefs_, readerScreen_.reader, true, nowMs);
    readerScreen_.book.mirror(readerScreen_.store, readerScreen_.reader);
    settingsStore_.flush();
    if (!usbTransfer_.begin(true)) {
        screens::status(immediateUi_, "USB", immediateUi_.text(UiText::CouldNotStart), usbTransfer_.statusMessage());
        delay(1200);
        screen_ = screens::Screen::Reader;
        renderScreen(nowMs);
        return;
    }
    readerScreen_.reader.pause();
    screen_ = screens::Screen::Usb;
    renderScreen(nowMs);
#else
    screens::status(immediateUi_, "USB", immediateUi_.text(UiText::Unavailable));
    delay(1000);
    screen_ = screens::Screen::Reader;
    renderScreen(nowMs);
#endif
}

void App::exitUsbTransfer(screens::Screen destination) {
    usbTransfer_.end();
    storage_.refreshBooks();
    libraryScreen_.invalidate();
    screen_ = destination;
    renderScreen(millis());
}

void App::reloadSettings(uint32_t nowMs) {
    readerScreen_.fonts.loadFromSd();
    interfaceScreen_.begin(immediateUi_, settingsStore_.settings().interface,
                           settingsStore_.settings().reading.typography, readerScreen_.fonts,
                           &Board::Display::setBrightness);
    readerScreen_.begin(settingsStore_.settings().reading, interfaceScreen_.themes.selected(), nowMs);
    networkScreen_.begin(settingsStore_);
    networkScreen_.startupCheckPending = false;
}

void App::runOtaCheck(bool install) {
    readerScreen_.reader.pause();
    if (install) {
        const uint32_t nowMs = millis();
        readerScreen_.book.save(prefs_, readerScreen_.reader, true, nowMs);
        readerScreen_.book.mirror(readerScreen_.store, readerScreen_.reader);
        settingsStore_.flush();
    }
    screens::status(immediateUi_, "OTA", immediateUi_.text(UiText::Checking));
    OtaUpdater ota;
    const OtaUpdater::Config config = ota.config(settingsStore_.settings(), settingsStore_.secrets());
    const OtaUpdater::Result result = install ? ota.checkAndInstall(config, &App::renderStorageStatus, this)
                                              : ota.checkOnly(config, &App::renderStorageStatus, this);
    screens::status(immediateUi_, "OTA", result.summary.c_str(), result.detail.c_str());
    delay(install && result.rebootRequired ? 500 : 1400);
    if (install && result.rebootRequired) {
        ESP.restart();
    }
    screen_ = screens::Screen::Reader;
    renderScreen(millis());
}

void App::enterStandby(uint32_t nowMs) {
    readerScreen_.book.save(prefs_, readerScreen_.reader, true, nowMs);
    readerScreen_.reader.pause();
    if (settingsStore_.settings().interface.screensaver == standby::Kind::screenOff) {
        screen_ = screens::Screen::Standby;
        lightSleepFromStandby();
        return;
    }
    Board::Display::wake();
    immediateUi_.invalidate();
    standbyScreen_.begin(immediateUi_, nowMs, readerScreen_.book.index, readerScreen_.reader.currentIndex(),
                         settingsStore_.settings().interface.screensaver);
    standbyEnteredMs_ = nowMs;
    screen_ = screens::Screen::Standby;
    renderScreen(nowMs);
}

void App::exitStandby(uint32_t nowMs) {
    standbyScreen_.reset();
    Board::Display::wake();
    lastActivityMs_ = nowMs;
    screen_ = screens::Screen::Reader;
    renderScreen(nowMs);
}

void App::lightSleepFromStandby() {
    ESP_LOGI("app", "screen-off standby; entering light sleep");
    readerScreen_.book.mirror(readerScreen_.store, readerScreen_.reader);
    standbyScreen_.reset();
    if (sync_.active())
        sync_.end();
    networkScreen_.closeWifi();
    if (usbTransfer_.active())
        usbTransfer_.end();
    settingsStore_.flush();
    Board::Display::sleep();
    Input::cancel();

    bool wokeByTouch = false;
    const uint32_t sleepStartedAtMs = millis();
    while (true) {
        const uint32_t elapsedMs = millis() - sleepStartedAtMs;
        if (elapsedMs >= kStandbyPowerOffMs) {
            ESP_LOGI("app", "screen-off standby expired; powering off");
            powerOff(millis());
            return;
        }

        switch (Board::System::lightSleep(kStandbyPowerOffMs - elapsedMs)) {
        case EspLightSleep::WakeReason::timer:
            ESP_LOGI("app", "screen-off standby expired; powering off");
            powerOff(millis());
            return;
        case EspLightSleep::WakeReason::input:
            if constexpr (Board::Config::HAS_LIGHT_SLEEP_TOUCH_IRQ) {
                ui::TouchContact contact = {};
                wokeByTouch = Board::Input::touchReady() && Board::Input::readTouch(contact) && contact.touched;
                const ::Input::PressActions controls = Board::Input::currentActions();
                const bool powerPressed = ::Input::hasAction(controls.longPress, ::Input::ActionPowerOff);
                if (!wokeByTouch && !powerPressed)
                    continue;
            }
            break;
        case EspLightSleep::WakeReason::error:
            break;
        }
        break;
    }

    const uint32_t wokeAtMs = millis();
    Input::cancel();
    exitStandby(wokeAtMs);

    if (wokeByTouch) {
        const uint32_t releaseWaitStartedMs = millis();
        ui::TouchContact contact = {.touched = true};
        while (contact.touched && millis() - releaseWaitStartedMs < 1000) {
            delay(10);
            if (!Board::Input::readTouch(contact))
                break;
        }
    }
}

void App::powerOff(uint32_t nowMs) {
    if (screen_ == screens::Screen::FocusSession)
        focusScreen_.close();
    readerScreen_.book.save(prefs_, readerScreen_.reader, true, nowMs);
    readerScreen_.book.mirror(readerScreen_.store, readerScreen_.reader);
    readerScreen_.reader.pause();
    screens::status(immediateUi_, immediateUi_.text(UiText::Off), immediateUi_.text(UiText::ReleasePower));
    delay(250);
    settingsStore_.flush();
    Board::Display::sleep();
    readerScreen_.store.close();
    storage_.end();
    Input::end();
    powerOffBoard();
}

void App::renderStorageStatus(void* context, const char* title, const char* line1, const char* line2,
                              int progressPercent) {
    if (context == nullptr) {
        return;
    }
    screens::status(static_cast<App*>(context)->immediateUi_, title == nullptr ? "SD" : title,
                    line1 == nullptr ? "" : line1, line2 == nullptr ? "" : line2, progressPercent);
}
