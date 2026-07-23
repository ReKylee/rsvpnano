#include "app/App.h"
#include <esp_log.h>

#include <array>
#include <cstdio>
#include <esp_system.h>
#include <string>

#include "board/BoardAudio.h"
#include "board/BoardConfig.h"
#include "board/BoardInput.h"
#include "board/BoardPower.h"
#include "board/BoardStorage.h"
#include "board/BoardSystem.h"
#include "freertos/task.h"
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
    constexpr UBaseType_t kJobQueueLength = 8;
    constexpr uint32_t kJobStackBytes = 12288;
    constexpr UBaseType_t kJobPriority = 1;

    template<size_t Size>
    void copyText(char (&destination)[Size], const char* source) {
        std::snprintf(destination, Size, "%s", source == nullptr ? "" : source);
    }

    bool deadlineReached(uint32_t nowMs, uint32_t deadlineMs) {
        return static_cast<int32_t>(nowMs - deadlineMs) >= 0;
    }

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
    bootMs_ = millis();
    lastActivityMs_ = bootMs_;
    statusUntilMs_ = bootMs_ + kBootSplashMs;

    if (!Board::Display::begin()) {
        ESP_LOGE("app", "display init failed");
    }
    immediateUi_.setOrientation(Board::Display::defaultUiOrientation());
    immediateUi_.setTheme(interfaceScreen_.themes.selected());
    screens::status(immediateUi_, immediateUi_.text(UiText::Ready));
    if (!Input::begin())
        ESP_LOGE("input", "startup failed");
    immediateUi_.setTouchSource({.surface = Board::Input::touchSurface(), .poll = &Input::pollTouch});

    storage_.begin();
    if (auto result = settingsStore_.begin(storage_.mounted() ? &Board::Storage::filesystem() : nullptr); !result)
        ESP_LOGW("settings", "startup warning: %s", result.error().message.c_str());
    migrateLegacyStorage();
    readerScreen_.fonts.loadFromSd();
    auto& deviceSettings = settingsStore_.settings();
    interfaceScreen_.begin(immediateUi_, deviceSettings.interface, deviceSettings.reading.typography,
                           readerScreen_.fonts, &Board::Display::setBrightness);
    Board::Power::updateBattery(battery_, bootMs_, true);
    readerScreen_.begin(interfaceScreen_.themes.selected());
    networkScreen_.begin(settingsStore_);
    focusScreen_.begin(storage_.mounted() ? &Board::Storage::filesystem() : nullptr);
    readerScreen_.loadInitialBook(immediateUi_, storage_, prefs_, bootMs_);
    libraryScreen_.invalidate();
}

void App::update(uint32_t nowMs) {
    Input::Event event;
    while (Input::poll(event)) {
        lastActivityMs_ = nowMs;
        handleInput(event, nowMs);
        nowMs = millis();
    }
    while (immediateUi_.pollTouch(nowMs)) {
        lastActivityMs_ = nowMs;
        handleTouch(nowMs);
        nowMs = millis();
    }

    updateBackgroundJob();
    if (backgroundJobActive())
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

    Board::Power::updateBattery(battery_, nowMs);
    readerScreen_.update(prefs_, nowMs);
    if (sync_.active() && sync_.update())
        reloadSettings();
    if (screen_ == screens::Screen::FocusSession) {
        if (focusScreen_.update(nowMs))
            Board::Audio::beep();
    }
    ReadingProgress::save(readerScreen_.session, prefs_, false, nowMs);

    renderScreen(nowMs);
    if (!readerScreen_.session.playing && !sync_.active() && !usbTransfer_.active()
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
        readerScreen_.draw(immediateUi_, storage_, battery_, nowMs);
        immediateUi_.endFrame();
        return;
    case screens::Screen::Library: {
        const auto& items = libraryScreen_.items(storage_, readerScreen_.store, readerScreen_.session);
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        const screens::LibraryResult result = libraryScreen_.draw(immediateUi_, items, nowMs, screen_);
        immediateUi_.endFrame();
        if (result.open) {
            runBookOpen(libraryScreen_.selectedIndex(), nowMs);
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
                                   {ReadingProgress::title(readerScreen_.session, storage_),
                                    readerScreen_.session.metadata.author,
                                    ReadingProgress::percent(readerScreen_.session.currentIndex,
                                                             ReadingLoop::wordCount(readerScreen_.session))},
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
        if (screens::readingSettings(immediateUi_, settingsStore_.settings().reading, screen_)) {
            settingsStore_.acceptChanges();
            if (mode != settingsStore_.settings().reading.mode)
                readerScreen_.refreshTypography();
        }
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
        if (screens::pacingSettings(immediateUi_, settingsStore_.settings().reading.pacing, screen_))
            settingsStore_.acceptChanges();
        break;
    }
    case screens::Screen::TypographySettings: {
        immediateUi_.beginFrame(static_cast<uint8_t>(screen_));
        if (screens::typographySettings(immediateUi_, readerScreen_.session.state.bookTypographyOverride,
                                        interfaceScreen_.themes.selected().definition.typography, readerScreen_.fonts,
                                        screen_)) {
            ReadingProgress::mirror(readerScreen_.session, readerScreen_.store);
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
    switch (action) {
    case screens::Action::None:
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
    case screens::Action::StorageStatus:
        jobStorageInventory_ = {
            .libraryItems = storage_.bookCount(),
            .fonts = readerScreen_.fonts.families().size() - 1,
            .themes = interfaceScreen_.themes.themes().size() - 1,
        };
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

void App::handleInput(const Input::Event& event, uint32_t nowMs) {
    if (screen_ == screens::Screen::Standby) {
        exitStandby(nowMs);
        return;
    }
    if (backgroundJobActive() || screen_ == screens::Screen::Status)
        return;
    if (usbTransfer_.active() && Input::hasAction(event.actions, Input::ActionPowerOff)) {
        exitUsbTransfer();
        return;
    }
    if (usbTransfer_.active() && Input::hasAction(event.actions, Input::ActionOpenMenu)) {
        exitUsbTransfer();
        return;
    }
    if (usbTransfer_.active() && Input::hasAction(event.actions, Input::ActionBack)) {
        exitUsbTransfer(screens::Screen::Read);
        return;
    }
    if (Input::hasAction(event.actions, Input::ActionOpenMenu)) {
        if (screen_ == screens::Screen::Reader) {
            ReadingProgress::save(readerScreen_.session, prefs_, true, nowMs);
            ReadingLoop::pause(readerScreen_.session);
            libraryScreen_.invalidate();
            screen_ = screens::Screen::Read;
        } else {
            if (sync_.active()) {
                sync_.end();
                reloadSettings();
            }
            if (screen_ == screens::Screen::FocusSession)
                focusScreen_.close();
            networkScreen_.closeWifi();
            ReadingLoop::pause(readerScreen_.session);
            screen_ = screens::Screen::Reader;
        }
        renderScreen(nowMs);
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
    if (Input::hasAction(event.actions, Input::ActionBack)) {
        if (sync_.active()) {
            sync_.end();
            reloadSettings();
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
            ReadingProgress::save(readerScreen_.session, prefs_, true, nowMs);
            ReadingLoop::pause(readerScreen_.session);
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

void App::showTransientStatus(std::string_view title, std::string_view line1, std::string_view line2,
                              uint32_t durationMs, screens::Screen destination, int progressPercent) {
    screen_ = screens::Screen::Status;
    statusDestination_ = destination;
    statusUntilMs_ = millis() + durationMs;
    restartAfterStatus_ = false;
    screens::status(immediateUi_, title, line1, line2, progressPercent);
}

void App::updateBackgroundJob() {
    JobUpdate update;
    while (jobQueue_ != nullptr && xQueueReceive(jobQueue_, &update, 0) == pdTRUE) {
        if (!update.complete) {
            screens::status(immediateUi_, update.title, update.line1, update.line2, update.progressPercent);
            continue;
        }

        const JobKind completed = jobKind_;
        jobKind_ = JobKind::None;
        if (completed == JobKind::Book) {
            if (jobBookLoaded_) {
                readerScreen_.finishBookOpen(prefs_, jobLoadedBookIndex_, jobBookPath_, millis());
                ReadingLoop::pause(readerScreen_.session);
                screen_ = screens::Screen::Reader;
                statusUntilMs_ = 0;
                renderScreen(millis());
            } else {
                showTransientStatus(immediateUi_.text(UiText::BookFailed), jobBookName_,
                                    immediateUi_.text(UiText::CheckSdCard), 1200, screens::Screen::Library);
            }
            return;
        }
        if (completed == JobKind::Rss) {
            libraryScreen_.invalidate();
            showTransientStatus("RSS", jobRssResult_.summary, jobRssResult_.detail, 1400, screens::Screen::Reader);
            return;
        }
        if (completed == JobKind::StorageCheck) {
            showTransientStatus(immediateUi_.text(UiText::Storage), jobStorageResult_.summary, jobStorageResult_.detail,
                                1800, screens::Screen::Device);
            return;
        }

        const bool reboot = completed == JobKind::OtaInstall && jobOtaResult_.rebootRequired;
        showTransientStatus("OTA", jobOtaResult_.summary, jobOtaResult_.detail, reboot ? 500 : 1400,
                            screens::Screen::Reader);
        restartAfterStatus_ = reboot;
        return;
    }
}

bool App::startBackgroundJob(JobKind kind) {
    if (backgroundJobActive())
        return false;
    if (jobQueue_ == nullptr)
        jobQueue_ = xQueueCreate(kJobQueueLength, sizeof(JobUpdate));
    if (jobQueue_ == nullptr)
        return false;
    xQueueReset(jobQueue_);
    jobKind_ = kind;
    if (xTaskCreate(backgroundJobEntry, "background", kJobStackBytes, this, kJobPriority, nullptr) != pdPASS) {
        jobKind_ = JobKind::None;
        return false;
    }
    return true;
}

void App::backgroundJobEntry(void* context) {
    ESP_LOGI("background", "started task=%s core=%d", pcTaskGetName(nullptr), xPortGetCoreID());
    static_cast<App*>(context)->runBackgroundJob();
    ESP_LOGI("background", "finished task=%s core=%d", pcTaskGetName(nullptr), xPortGetCoreID());
    vTaskDelete(nullptr);
}

void App::runBackgroundJob() {
    switch (jobKind_) {
    case JobKind::Rss: {
        Preferences preferences;
        if (!preferences.begin(settings::kStateNvsNamespace)) {
            jobRssResult_.summary = "RSS failed";
            jobRssResult_.detail = "Could not open state";
        } else {
            jobRssResult_ = RssFeeds::check(preferences, jobSettings_, jobSecrets_, &App::renderStorageStatus, this);
            preferences.end();
        }
        storage_.refreshBooks();
        break;
    }
    case JobKind::StorageCheck:
        jobStorageResult_ = SdDiagnostics::run(storage_.mounted(), jobStorageInventory_);
        break;
    case JobKind::OtaCheck:
        jobOtaResult_ = OtaUpdater::checkOnly(jobOtaConfig_, &App::renderStorageStatus, this);
        break;
    case JobKind::OtaInstall:
        jobOtaResult_ = OtaUpdater::checkAndInstall(jobOtaConfig_, &App::renderStorageStatus, this);
        break;
    case JobKind::Book: {
        StorageManager::IndexedBookLoadOptions options;
        options.loadedPath = &jobBookPath_;
        options.loadedIndex = &jobLoadedBookIndex_;
        jobBookLoaded_ =
            storage_.loadIndexedBook(jobBookIndex_, readerScreen_.store, readerScreen_.session.metadata, options);
        break;
    }
    case JobKind::None:
        break;
    }

    JobUpdate complete;
    complete.complete = true;
    enqueueJobUpdate(complete, true);
}

void App::enqueueJobUpdate(JobUpdate update, bool mustSucceed) {
    if (mustSucceed) {
        xQueueSend(jobQueue_, &update, portMAX_DELAY);
        return;
    }
    if (xQueueSend(jobQueue_, &update, 0) == pdTRUE)
        return;
    JobUpdate discarded;
    xQueueReceive(jobQueue_, &discarded, 0);
    xQueueSend(jobQueue_, &update, 0);
}

void App::runRss() {
    ReadingLoop::pause(readerScreen_.session);
    screen_ = screens::Screen::Status;
    statusUntilMs_ = 0;
    screens::status(immediateUi_, "RSS", immediateUi_.text(UiText::CheckingFeeds));
    jobSettings_ = settingsStore_.settings();
    jobSecrets_ = settingsStore_.secrets();
    if (!startBackgroundJob(JobKind::Rss))
        showTransientStatus("RSS", immediateUi_.text(UiText::CouldNotStart), {}, 1200, screens::Screen::Reader);
}

void App::runBookOpen(size_t index, uint32_t nowMs) {
    if (!storage_.mounted() || index >= storage_.bookCount())
        return;

    jobBookIndex_ = index;
    jobLoadedBookIndex_ = index;
    jobBookPath_.clear();
    jobBookName_ = storage_.bookDisplayName(index);
    jobBookLoaded_ = false;
    screens::status(immediateUi_, immediateUi_.text(UiText::OpeningBook), jobBookName_, {}, 5);
    screen_ = screens::Screen::Status;
    statusUntilMs_ = 0;
    readerScreen_.prepareBookOpen(prefs_, nowMs);
    if (!startBackgroundJob(JobKind::Book))
        showTransientStatus(immediateUi_.text(UiText::BookFailed), jobBookName_,
                            immediateUi_.text(UiText::CheckSdCard), 1200, screens::Screen::Library);
}

void App::enterUsbTransfer(uint32_t nowMs) {
#if RSVP_USB_TRANSFER_ENABLED
    ReadingProgress::save(readerScreen_.session, prefs_, true, nowMs);
    ReadingProgress::mirror(readerScreen_.session, readerScreen_.store);
    settingsStore_.flush();
    if (!usbTransfer_.begin(true)) {
        showTransientStatus("USB", immediateUi_.text(UiText::CouldNotStart), usbTransfer_.statusMessage(), 1200,
                            screens::Screen::Reader);
        return;
    }
    ReadingLoop::pause(readerScreen_.session);
    screen_ = screens::Screen::Usb;
    renderScreen(nowMs);
#else
    showTransientStatus("USB", immediateUi_.text(UiText::Unavailable), {}, 1000, screens::Screen::Reader);
#endif
}

void App::exitUsbTransfer(screens::Screen destination) {
    usbTransfer_.end();
    storage_.refreshBooks();
    libraryScreen_.invalidate();
    screen_ = destination;
    renderScreen(millis());
}

void App::reloadSettings() {
    readerScreen_.fonts.loadFromSd();
    interfaceScreen_.begin(immediateUi_, settingsStore_.settings().interface,
                           settingsStore_.settings().reading.typography, readerScreen_.fonts,
                           &Board::Display::setBrightness);
    readerScreen_.begin(interfaceScreen_.themes.selected());
    networkScreen_.begin(settingsStore_);
    networkScreen_.startupCheckPending = false;
}

void App::runOtaCheck(bool install) {
    ReadingLoop::pause(readerScreen_.session);
    if (install) {
        const uint32_t nowMs = millis();
        ReadingProgress::save(readerScreen_.session, prefs_, true, nowMs);
        ReadingProgress::mirror(readerScreen_.session, readerScreen_.store);
        settingsStore_.flush();
    }
    screen_ = screens::Screen::Status;
    statusUntilMs_ = 0;
    screens::status(immediateUi_, "OTA", immediateUi_.text(UiText::Checking));
    jobOtaConfig_ = OtaUpdater::config(settingsStore_.settings(), settingsStore_.secrets());
    if (!startBackgroundJob(install ? JobKind::OtaInstall : JobKind::OtaCheck))
        showTransientStatus("OTA", immediateUi_.text(UiText::CouldNotStart), {}, 1200, screens::Screen::Reader);
}

void App::enterStandby(uint32_t nowMs) {
    ReadingProgress::save(readerScreen_.session, prefs_, true, nowMs);
    ReadingLoop::pause(readerScreen_.session);
    if (settingsStore_.settings().interface.screensaver == standby::Kind::screenOff) {
        screen_ = screens::Screen::Standby;
        lightSleepFromStandby();
        return;
    }
    Board::Display::wake();
    immediateUi_.invalidate();
    standbyScreen_.begin(immediateUi_, nowMs, readerScreen_.session.bookIndex, readerScreen_.session.currentIndex,
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
    ReadingProgress::mirror(readerScreen_.session, readerScreen_.store);
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
    Input::resume();
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
    ReadingProgress::save(readerScreen_.session, prefs_, true, nowMs);
    ReadingProgress::mirror(readerScreen_.session, readerScreen_.store);
    ReadingLoop::pause(readerScreen_.session);
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
    if (context == nullptr)
        return;

    App& app = *static_cast<App*>(context);
    if (app.backgroundJobActive()) {
        JobUpdate update;
        copyText(update.title, title == nullptr ? "SD" : title);
        copyText(update.line1, line1);
        copyText(update.line2, line2);
        update.progressPercent = progressPercent;
        app.enqueueJobUpdate(update);
        return;
    }
    screens::status(app.immediateUi_, title == nullptr ? "SD" : title, line1 == nullptr ? "" : line1,
                    line2 == nullptr ? "" : line2, progressPercent);
}
