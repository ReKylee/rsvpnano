#include "app/App.h"

#include <string>
#include "app/screens/reader/ReadScreen.h"
#include "app/screens/settings/SettingsScreens.h"
#include "app/screens/reader/BookFontsScreen.h"
#include "app/screens/device/DeviceScreen.h"
#include "app/screens/device/OtaScreen.h"
#include "app/screens/status/StatusScreen.h"
#include "app/screens/standby/StandbyTiming.h"
#include "settings/NvsSecurity.h"
#include "library/ReadingProgress.h"

void App::renderScreen(uint32_t nowMs) {
    if (serialCompanion_.active()) {
        companionApi_.renderStatus(true);
        return;
    }
    const screens::Screen renderedScreen = screen_;
    screens::Action action = screens::Action::None;
    switch (screen_) {
    case screens::Screen::Status:
        screens::status(immediateUi_, immediateUi_.text(UiText::Ready));
        return;
    case screens::Screen::Reader:
        if (typographyJobActive()) {
            screens::status(immediateUi_, immediateUi_.text(UiText::FontSection), immediateUi_.text(UiText::Checking));
            return;
        }
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        readerScreen_.draw(immediateUi_, storage_, battery_, nowMs);
        immediateUi_.endFrame();
        return;
    case screens::Screen::Library: {
        const auto& items = libraryScreen_.items(storage_, readerScreen_.store, readerScreen_.session);
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        const screens::Action result = libraryScreen_.draw(immediateUi_, items, nowMs, screen_);
        immediateUi_.endFrame();
        if (result == screens::Action::OpenBook) {
            runBookOpen(libraryScreen_.selectedIndex(), nowMs);
        } else {
            handleScreenAction(result, nowMs);
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
            const int bookIndex = storage_.findBook(readerScreen_.session.sourcePath());
            const BookLibrary::Entry* book = bookIndex < 0 ? nullptr : storage_.book(static_cast<size_t>(bookIndex));
            action = screens::read(immediateUi_, ReadingProgress::title(readerScreen_.session, storage_),
                                   book == nullptr ? std::string_view{} : std::string_view{book->author},
                                   ReadingProgress::percent(readerScreen_.session.state.wordIndex,
                                                            ReadingLoop::wordCount(readerScreen_.session)),
                                   screen_);
        }
        break;
    case screens::Screen::Chapters:
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        action = chaptersScreen_.draw(immediateUi_, readerScreen_.session.metadata.chapters, readerScreen_.session,
                                      settingsStore_.settings().reading, nowMs, screen_);
        break;
    case screens::Screen::Settings:
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        action = screens::settings(immediateUi_, screen_);
        break;
    case screens::Screen::ReadingSettings: {
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        const settings::ReadingMode mode = settingsStore_.settings().reading.mode;
        const bool leftHanded = settingsStore_.settings().reading.leftHanded;
        if (screens::readingSettings(immediateUi_, settingsStore_.settings().reading, screen_)) {
            settingsStore_.acceptChanges();
            if (mode != settingsStore_.settings().reading.mode)
                requestTypographyRefresh();
            if (leftHanded != settingsStore_.settings().reading.leftHanded) {
                immediateUi_.endFrame();
                immediateUi_.setOrientation(settingsStore_.settings().reading.leftHanded
                                                ? Board::Display::rotatedUiOrientation()
                                                : Board::Display::defaultUiOrientation());
                renderScreen(nowMs);
                return;
            }
        }
        break;
    }
    case screens::Screen::InterfaceSettings: {
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        const std::string locale = settingsStore_.settings().interface.locale;
        if (interfaceScreen_.draw(immediateUi_, settingsStore_.settings().interface, screens::kStandbyDurationsMs,
                                  &Board::Display::setBrightness, screen_)) {
            settingsStore_.acceptChanges();
            if (locale != settingsStore_.settings().interface.locale)
                reloadUiAssets();
            readerScreen_
                .applyTheme(interfaceScreen_.themes.resolve(settingsStore_.settings().interface.selectedThemeId));
        }
        break;
    }
    case screens::Screen::PacingSettings: {
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        if (screens::pacingSettings(immediateUi_, settingsStore_.settings().reading.pacing, screen_))
            settingsStore_.acceptChanges();
        break;
    }
    case screens::Screen::ReaderAppearance: {
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        if (readerScreen_.appearance(immediateUi_, screen_)) {
            settingsStore_.acceptChanges();
        }
        break;
    }
    case screens::Screen::BookFonts: {
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        if (screens::bookFonts(immediateUi_, readerScreen_.session.metadata, readerScreen_.session.state.overrides,
                               localeCatalog_, readerScreen_.fonts, screen_)) {
            ReadingProgress::mirror(readerScreen_.session, readerScreen_.store);
            requestTypographyRefresh();
        }
        break;
    }
    case screens::Screen::NetworkSettings:
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        action = networkScreen_.draw(immediateUi_, settingsStore_, screen_);
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
        action = screens::device(immediateUi_, storage_.mounted(), storage_.books().size(),
                                 settings::nvsEncryptionState(), screen_);
        break;
    case screens::Screen::StorageEncryption:
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        action = screens::storageEncryption(immediateUi_, settings::nvsEncryptionState(), screen_);
        break;
    case screens::Screen::Sync:
        screens::status(immediateUi_, immediateUi_.text(UiText::Sync), companionApi_.statusLine1(),
                        companionApi_.statusLine2());
        return;
    case screens::Screen::Ota: {
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        action = screens::ota(immediateUi_, OtaUpdater::currentVersion().data(), screen_);
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
    if (backgroundJobActive() && action != screens::Action::None)
        return;
    switch (action) {
    case screens::Action::None:
    case screens::Action::OpenBook:
        return;
    case screens::Action::Resume:
        ReadingLoop::pause(readerScreen_.session);
        screen_ = screens::Screen::Reader;
        renderScreen(nowMs);
        return;
    case screens::Action::PowerOff:
        powerOff(nowMs);
        return;
    case screens::Action::CompanionSync:
        screen_ = screens::Screen::Sync;
        immediateUi_.invalidate();
        screens::status(immediateUi_, immediateUi_.text(UiText::CompanionSync), immediateUi_.text(UiText::Connecting));
        companionApi_.begin();
        renderScreen(nowMs);
        return;
    case screens::Action::RssRefresh:
        runRss();
        return;
    case screens::Action::UsbTransfer:
        enterUsbTransfer(nowMs);
        return;
    case screens::Action::StorageStatus:
        screen_ = screens::Screen::Status;
        statusUntilMs_ = 0;
        screens::status(immediateUi_, immediateUi_.text(UiText::Storage), immediateUi_.text(UiText::Checking));
        if (!startBackgroundJob(JobKind::StorageCheck))
            showTransientStatus(immediateUi_.text(UiText::Storage), immediateUi_.text(UiText::CouldNotStart), {}, 1200,
                                screens::Screen::Device);
        return;
    case screens::Action::EnableStorageEncryption:
        screens::status(immediateUi_, immediateUi_.text(UiText::StorageEncryption),
                        immediateUi_.text(UiText::EnablingEncryption));
        ReadingProgress::save(readerScreen_.session, prefs_, true, nowMs);
        ReadingProgress::mirror(readerScreen_.session, readerScreen_.store);
        if (!storage_.mounted() || !settings::enableNvsEncryption(prefs_, settingsStore_)) {
            showTransientStatus(immediateUi_.text(UiText::StorageEncryption), immediateUi_.text(UiText::Unavailable),
                                {}, 1200, screens::Screen::Device);
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

void App::handleInput(Input::ActionMask actions, uint32_t nowMs) {
    if (serialCompanion_.active()) {
        if (Input::hasAction(actions, Input::ActionBack) || Input::hasAction(actions, Input::ActionOpenMenu)) {
            serialCompanion_.close();
            renderScreen(nowMs);
        }
        return;
    }
    if (screen_ == screens::Screen::Standby) {
        exitStandby(nowMs);
        return;
    }
    if (backgroundJobActive() || screen_ == screens::Screen::Status)
        return;
    if (usbTransfer_.active() && Input::hasAction(actions, Input::ActionPowerOff)) {
        exitUsbTransfer();
        return;
    }
    if (usbTransfer_.active() && Input::hasAction(actions, Input::ActionOpenMenu)) {
        exitUsbTransfer();
        return;
    }
    if (usbTransfer_.active() && Input::hasAction(actions, Input::ActionBack)) {
        exitUsbTransfer(screens::Screen::Read);
        return;
    }
    if (Input::hasAction(actions, Input::ActionOpenMenu)) {
        if (screen_ == screens::Screen::Reader) {
            ReadingProgress::save(readerScreen_.session, prefs_, true, nowMs);
            ReadingLoop::pause(readerScreen_.session);
            libraryScreen_.invalidate();
            screen_ = screens::Screen::Read;
        } else {
            if (companionApi_.active()) {
                companionApi_.end();
            } else {
                networkScreen_.closeWifi();
            }
            if (screen_ == screens::Screen::FocusSession)
                focusScreen_.close();
            ReadingLoop::pause(readerScreen_.session);
            screen_ = screens::Screen::Reader;
        }
        renderScreen(nowMs);
        return;
    }
    if (screen_ == screens::Screen::FocusSession
        && (Input::hasAction(actions, Input::ActionBack) || Input::hasAction(actions, Input::ActionSelect))) {
        focusScreen_.close();
        screen_ = screens::Screen::FocusTimers;
        renderScreen(nowMs);
        return;
    }
    if (Input::hasAction(actions, Input::ActionPowerOff)) {
        powerOff(nowMs);
        return;
    }
    if (Input::hasAction(actions, Input::ActionStandby)) {
        enterStandby(nowMs);
        return;
    }
    if (Input::hasAction(actions, Input::ActionBack)) {
        if (companionApi_.active()) {
            companionApi_.end();
            screen_ = screens::Screen::Device;
            renderScreen(nowMs);
        } else if (screen_ != screens::Screen::Reader) {
            if (screen_ == screens::Screen::Read) {
                ReadingLoop::pause(readerScreen_.session);
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
                       || screen_ == screens::Screen::PacingSettings || screen_ == screens::Screen::ReaderAppearance
                       || screen_ == screens::Screen::NetworkSettings) {
                screen_ = screens::Screen::Settings;
            } else if (screen_ == screens::Screen::BookFonts) {
                screen_ = screens::Screen::Read;
            } else if (screen_ == screens::Screen::FocusNameEdit) {
                screen_ = screens::Screen::FocusEditor;
            } else if (screen_ == screens::Screen::FocusEditor) {
                screen_ = screens::Screen::FocusTimers;
            } else {
                screen_ = screens::Screen::Read;
            }
            renderScreen(nowMs);
        } else {
            ReadingProgress::save(readerScreen_.session, prefs_, true, nowMs);
            ReadingLoop::pause(readerScreen_.session);
            libraryScreen_.invalidate();
            screen_ = screens::Screen::Read;
            renderScreen(nowMs);
        }
        return;
    }
    if (Input::hasAction(actions, Input::ActionSelect) || Input::hasAction(actions, Input::ActionPlayPause)) {
        if (screen_ == screens::Screen::Reader && !typographyJobActive()) {
            readerScreen_.toggle(prefs_, nowMs);
        }
    }
}

void App::handleTouch(uint32_t nowMs) {
    if (serialCompanion_.active())
        return;
    const ui::Touch* touch = immediateUi_.touch();
    if (touch == nullptr)
        return;
    if (screen_ == screens::Screen::Standby) {
        exitStandby(nowMs);
        return;
    }
    if (companionApi_.active() || usbTransfer_.active() || backgroundJobActive() || screen_ == screens::Screen::Status)
        return;
    if (screen_ == screens::Screen::Reader) {
        readerScreen_.handleTouch(immediateUi_, nowMs, prefs_, settingsStore_);
    } else {
        renderScreen(nowMs);
    }
}

void App::showTransientStatus(std::string_view title, std::string_view line1, std::string_view line2,
                              uint32_t durationMs, screens::Screen destination, int progressPercent) {
    screen_ = screens::Screen::Status;
    statusDestination_ = destination;
    statusUntilMs_ = millis() + durationMs;
    restartAfterStatus_ = false;
    screens::status(immediateUi_, title, line1, line2, progressPercent);
}
