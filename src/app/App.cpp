#include "app/App.h"

#include <array>
#include <string>

#include "board/BoardAudio.h"
#include "board/BoardImu.h"
#include "board/BoardInput.h"
#include "board/BoardPower.h"
#include "board/BoardSystem.h"
#include "rss/RssFeedManager.h"
#include "settings/PreferenceSpecs.h"
#include "storage/index/ReadingProgress.h"
#include "update/OtaUpdater.h"

namespace {

    constexpr uint32_t kBootSplashMs = 650;
    constexpr std::array<uint32_t, 5> kStandbyMs = {
        0, 1UL * 60UL * 1000UL, 5UL * 60UL * 1000UL, 15UL * 60UL * 1000UL, 30UL * 60UL * 1000UL,
    };
} // namespace

void App::begin() {
    prefs_.begin(settings::kPrefsNamespace, false);
    storage_.setStatusCallback(&App::renderStorageStatus, this);
    Input::begin();
    bootMs_ = millis();
    lastActivityMs_ = bootMs_;
    immediateUi_.setTouchSource({Board::Input::touchSurface(), Board::Input::touchTiming(), &Board::Input::beginTouch,
                                 &Board::Input::touchReady, &Board::Input::readTouch, &Board::Imu::uiOrientation},
                                bootMs_);

    if (!Board::Display::begin()) {
        Serial.println("[app] display init failed");
    }
    immediateUi_.setTheme(interfaceScreen_.themes.selected());
    screens::status(immediateUi_, immediateUi_.text(UiText::Ready));

    storage_.begin();
    readerScreen_.begin(prefs_, bootMs_);
    interfaceScreen_.begin(immediateUi_, prefs_, kStandbyMs.size(), &Board::Display::setBrightness);
    networkScreen_.begin(prefs_);
    focusScreen_.timer.begin();
    readerScreen_.loadInitialBook(immediateUi_, storage_, prefs_, bootMs_);
    libraryScreen_.invalidate();
}

void App::update(uint32_t nowMs) {
    Input::Event event;
    while (Input::poll(event, nowMs)) {
        lastActivityMs_ = nowMs;
        handleInput(event, nowMs);
    }
    if (immediateUi_.pollTouch(nowMs)) {
        lastActivityMs_ = nowMs;
        handleTouch(nowMs);
    }

    if (screen_ == screens::Screen::Standby) {
        if (standbyScreen_.update(immediateUi_, nowMs))
            deepSleepFromStandby(nowMs);
        return;
    }

    if (screen_ == screens::Screen::Status && nowMs - bootMs_ >= kBootSplashMs) {
        screen_ = screens::Screen::Reader;
        if (networkScreen_.autoCheckPending) {
            networkScreen_.autoCheckPending = false;
            runOtaCheck(false);
            return;
        }
    }

    readerScreen_.battery.update(nowMs);
    readerScreen_.update(prefs_, nowMs);
    if (sync_.active()) {
        sync_.update();
    }
    if (screen_ == screens::Screen::FocusSession) {
        focusScreen_.timer.update(nowMs);
        if (focusScreen_.timer.consumeCompletionCue())
            Board::Audio::beep();
    }
    readerScreen_.book.save(prefs_, readerScreen_.store, readerScreen_.reader, false, nowMs);

    renderScreen(nowMs);
    if (!readerScreen_.reader.playing() && !sync_.active() && !usbTransfer_.active()
        && screen_ != screens::Screen::FocusSession && screen_ != screens::Screen::Status
        && kStandbyMs[interfaceScreen_.config.standbyIndex] > 0
        && nowMs - lastActivityMs_ >= kStandbyMs[interfaceScreen_.config.standbyIndex]) {
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
            libraryScreen_.items(storage_, readerScreen_.store, readerScreen_.reader, readerScreen_.book, prefs_);
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
    case screens::Screen::FocusSession:
        if (focusScreen_.session(immediateUi_, nowMs)) {
            focusScreen_.timer.abandon();
            readerScreen_.reader.pause();
            screen_ = screens::Screen::Reader;
            renderScreen(nowMs);
        }
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
                                      readerScreen_.session.settings, nowMs, screen_);
        break;
    case screens::Screen::Settings:
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        action = screens::settings(immediateUi_, screen_);
        break;
    case screens::Screen::ReadingSettings: {
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        screens::readingSettings(immediateUi_, readerScreen_.reader, readerScreen_.session.settings, prefs_, screen_);
        break;
    }
    case screens::Screen::InterfaceSettings: {
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        interfaceScreen_.draw(immediateUi_, prefs_, kStandbyMs, &Board::Display::setBrightness, screen_);
        break;
    }
    case screens::Screen::PacingSettings: {
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        screens::pacingSettings(immediateUi_, readerScreen_.reader, prefs_, screen_);
        break;
    }
    case screens::Screen::TypographySettings: {
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        screens::typographySettings(immediateUi_, readerScreen_.session.settings, readerScreen_.fonts, prefs_, screen_);
        break;
    }
    case screens::Screen::ReaderSettings:
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        screens::readerSettings(immediateUi_, readerScreen_.session.settings, prefs_, screen_);
        break;
    case screens::Screen::NetworkSettings:
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        networkScreen_.draw(immediateUi_, prefs_, screen_);
        break;
    case screens::Screen::WifiScan:
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        networkScreen_.drawWifiScan(immediateUi_, prefs_, screen_);
        break;
    case screens::Screen::WifiConnect:
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        if (networkScreen_.drawWifiConnect(immediateUi_, prefs_, screen_))
            return;
        break;
    case screens::Screen::NetworkEdit:
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        networkScreen_.drawEdit(immediateUi_, prefs_, screen_);
        break;
    case screens::Screen::Device:
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        action = screens::device(immediateUi_, storage_.mounted(), storage_.bookCount(), screen_);
        break;
    case screens::Screen::Sync:
        if (sync_.active()) {
            screens::status(immediateUi_, immediateUi_.text(UiText::Sync), sync_.statusLine1(), sync_.statusLine2());
            return;
        }
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        action = screens::sync(immediateUi_, screen_);
        break;
    case screens::Screen::Ota:
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        action = screens::ota(immediateUi_, screen_);
        break;
    case screens::Screen::FocusGenres:
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        if (focusScreen_.genres(immediateUi_, nowMs, screen_)) {
            immediateUi_.endFrame();
            screen_ = screens::Screen::FocusSession;
            renderScreen(nowMs);
            return;
        }
        break;
    }
    immediateUi_.endFrame();
    if (screen_ != renderedScreen) {
        if (screen_ == screens::Screen::Library)
            libraryScreen_.reset();
        if (screen_ == screens::Screen::FocusGenres)
            focusScreen_.timer.open();
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
        const std::string entries = std::to_string(storage_.bookCount()) + " "
                                  + std::string(immediateUi_.text(UiText::LibraryEntries));
        screens::status(immediateUi_, immediateUi_.text(UiText::Storage),
                        immediateUi_.text(storage_.mounted() ? UiText::SdReady : UiText::SdUnavailable), entries);
        delay(1200);
        renderScreen(nowMs);
        break;
    }
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
    if (screen_ == screens::Screen::FocusSession
        && (Input::hasAction(event.actions, Input::ActionBack)
            || Input::hasAction(event.actions, Input::ActionSelect))) {
        focusScreen_.timer.abandon();
        readerScreen_.reader.pause();
        screen_ = screens::Screen::Reader;
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
            readerScreen_.begin(prefs_, nowMs);
            interfaceScreen_.begin(immediateUi_, prefs_, kStandbyMs.size(), &Board::Display::setBrightness);
            networkScreen_.begin(prefs_);
            networkScreen_.autoCheckPending = false;
            screen_ = screens::Screen::Device;
            renderScreen(nowMs);
        } else if (screen_ != screens::Screen::Reader) {
            if (screen_ == screens::Screen::Read) {
                readerScreen_.reader.pause();
                screen_ = screens::Screen::Reader;
            } else if (screen_ == screens::Screen::Sync || screen_ == screens::Screen::Ota) {
                screen_ = screens::Screen::Device;
            } else if (screen_ == screens::Screen::WifiConnect) {
                networkScreen_.closeWifi();
                screen_ = screens::Screen::WifiScan;
            } else if (screen_ == screens::Screen::WifiScan || screen_ == screens::Screen::NetworkEdit) {
                if (screen_ == screens::Screen::WifiScan)
                    networkScreen_.closeWifi();
                screen_ = screens::Screen::NetworkSettings;
            } else if (screen_ == screens::Screen::ReadingSettings || screen_ == screens::Screen::InterfaceSettings
                       || screen_ == screens::Screen::PacingSettings
                       || screen_ == screens::Screen::TypographySettings
                       || screen_ == screens::Screen::ReaderSettings
                       || screen_ == screens::Screen::NetworkSettings) {
                screen_ = screens::Screen::Settings;
            } else {
                screen_ = screens::Screen::Read;
            }
            renderScreen(nowMs);
        } else {
            readerScreen_.book.save(prefs_, readerScreen_.store, readerScreen_.reader, true, nowMs);
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
        readerScreen_.handleTouch(immediateUi_, nowMs, prefs_);
    } else {
        renderScreen(nowMs);
    }
}

void App::runRss() {
    readerScreen_.reader.pause();
    screens::status(immediateUi_, "RSS", immediateUi_.text(UiText::CheckingFeeds));
    OtaUpdater ota;
    RssFeedManager rss;
    const RssFeedManager::Result result = rss.checkFeeds(ota.config(prefs_), prefs_, &App::renderStorageStatus, this);
    storage_.refreshBooks();
    libraryScreen_.invalidate();
    screens::status(immediateUi_, "RSS", result.summary.c_str(), result.detail.c_str());
    delay(1400);
    screen_ = screens::Screen::Reader;
    renderScreen(millis());
}

void App::enterUsbTransfer(uint32_t nowMs) {
#if RSVP_USB_TRANSFER_ENABLED
    readerScreen_.book.save(prefs_, readerScreen_.store, readerScreen_.reader, true, nowMs);
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

void App::exitUsbTransfer() {
    usbTransfer_.end();
    storage_.refreshBooks();
    libraryScreen_.invalidate();
    screen_ = screens::Screen::Reader;
    renderScreen(millis());
}

void App::runOtaCheck(bool install) {
    readerScreen_.reader.pause();
    screens::status(immediateUi_, "OTA", immediateUi_.text(UiText::Checking));
    OtaUpdater ota;
    const OtaUpdater::Config config = ota.config(prefs_);
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
    readerScreen_.book.save(prefs_, readerScreen_.store, readerScreen_.reader, true, nowMs);
    readerScreen_.reader.pause();
    Board::Display::wake();
    immediateUi_.invalidate();
    standbyScreen_.begin(immediateUi_, nowMs, readerScreen_.book.index, readerScreen_.reader.currentIndex(),
                         interfaceScreen_.config.screensaver);
    if (interfaceScreen_.config.screensaver == standby::Kind::ScreenOff)
        Board::Display::sleep();
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

void App::deepSleepFromStandby(uint32_t nowMs) {
    Serial.println("[app] standby timeout reached; entering deep sleep");
    readerScreen_.book.save(prefs_, readerScreen_.store, readerScreen_.reader, true, nowMs);
    readerScreen_.book.mirror(readerScreen_.store, readerScreen_.reader);
    standbyScreen_.reset();
    if (sync_.active())
        sync_.end();
    if (usbTransfer_.active())
        usbTransfer_.end();
    Board::Display::sleep();
    readerScreen_.store.close();
    storage_.end();
    Input::end();
    prefs_.end();
    Board::System::holdBacklightOffForDeepSleep();
    Serial.flush();
    Board::System::deepSleepUntilConfiguredWake();
}

void App::powerOff(uint32_t nowMs) {
    readerScreen_.book.save(prefs_, readerScreen_.store, readerScreen_.reader, true, nowMs);
    readerScreen_.book.mirror(readerScreen_.store, readerScreen_.reader);
    readerScreen_.reader.pause();
    screens::status(immediateUi_, immediateUi_.text(UiText::Off), immediateUi_.text(UiText::ReleasePower));
    delay(250);
    Board::Display::sleep();
    readerScreen_.store.close();
    storage_.end();
    Input::end();
    Board::System::holdBacklightOffForDeepSleep();
    if (Board::Power::shouldRequestShutdownOnPowerOff() || Board::Power::shouldReleaseBatteryPowerBeforeDeepSleep()) {
        Board::Power::releaseBatteryPowerHold();
        delay(1200);
    }
    Board::System::deepSleepUntilConfiguredWake();
}

void App::renderStorageStatus(void* context, const char* title, const char* line1, const char* line2,
                              int progressPercent) {
    if (context == nullptr) {
        return;
    }
    screens::status(static_cast<App*>(context)->immediateUi_, title == nullptr ? "SD" : title,
                    line1 == nullptr ? "" : line1, line2 == nullptr ? "" : line2, progressPercent);
}
