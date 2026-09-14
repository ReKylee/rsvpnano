#include "app/App.h"

#include <cstdio>
#include <string>
#include <esp_log.h>
#include "app/screens/status/StatusScreen.h"
#include "board/BoardStorage.h"
#include "freertos/task.h"
#include "logging/Logger.h"
#include "feeds/RssFeeds.h"
#include "settings/NvsSecurity.h"
#include "storage/migration/Migration.h"
#include "update/OtaUpdater.h"

namespace {
    constexpr UBaseType_t kJobQueueLength = 8;
    constexpr uint32_t kJobStackBytes = 12288;
    constexpr UBaseType_t kJobPriority = 1;
    template<size_t Size>
    void copyText(char (&destination)[Size], const char* source) {
        std::snprintf(destination, Size, "%s", source == nullptr ? "" : source);
    }
} // namespace

void App::updateBackgroundJob() {
    JobUpdate update;
    while (jobQueue_ != nullptr && xQueueReceive(jobQueue_, &update, 0) == pdTRUE) {
        if (!update.complete) {
            screens::status(immediateUi_, update.title, update.line1, update.line2, update.progressPercent);
            continue;
        }

        const JobKind completed = jobKind_;
        jobKind_ = JobKind::None;
        vQueueDelete(jobQueue_);
        jobQueue_ = nullptr;
        Logger::checkpoint("running");
        if (completed == JobKind::Typography) {
            if (bookOpenPending_) {
                const size_t index = pendingBookIndex_;
                bookOpenPending_ = false;
                typographyRefreshPending_ = false;
                typographyOpensBook_ = false;
                runBookOpen(index, millis());
                return;
            }
            if (typographyRefreshPending_) {
                typographyRefreshPending_ = false;
                requestTypographyRefresh();
                return;
            }
            immediateUi_.invalidate();
            if (typographyOpensBook_) {
                typographyOpensBook_ = false;
                ReadingLoop::pause(readerScreen_.session);
                screen_ = screens::Screen::Reader;
                statusUntilMs_ = 0;
            }
            renderScreen(millis());
            return;
        }
        if (completed == JobKind::Book) {
            const int loadedIndex = storage_.findBook(readerScreen_.store.sourcePath());
            const BookLibrary::Entry* book =
                storage_.book(jobBookLoaded_ && loadedIndex >= 0 ? static_cast<size_t>(loadedIndex) : jobBookIndex_);
            const std::string_view bookName = book == nullptr ? std::string_view{} : BookLibrary::displayName(*book);
            if (jobBookLoaded_) {
                readerScreen_.finishBookOpen(prefs_, millis());
                ReadingLoop::pause(readerScreen_.session);
                typographyOpensBook_ = true;
                screens::status(immediateUi_, immediateUi_.text(UiText::OpeningBook), bookName, {}, 85);
                if (!requestTypographyRefresh()) {
                    typographyOpensBook_ = false;
                    screen_ = screens::Screen::Reader;
                    statusUntilMs_ = 0;
                    renderScreen(millis());
                }
            } else {
                showTransientStatus(immediateUi_.text(UiText::BookFailed), bookName,
                                    immediateUi_.text(UiText::CheckSdCard), 1200, screens::Screen::Library);
            }
            return;
        }
        if (completed == JobKind::Rss) {
            libraryScreen_.invalidate();
            showTransientStatus("RSS", update.line1, update.line2, 1400, screens::Screen::Reader);
            return;
        }
        if (completed == JobKind::StorageCheck) {
            showTransientStatus(immediateUi_.text(UiText::Storage), update.line1, update.line2, 1800,
                                screens::Screen::Device);
            return;
        }

        const bool reboot = completed == JobKind::OtaInstall && update.rebootRequired;
        showTransientStatus("OTA", update.line1, update.line2, reboot ? 500 : 1400, screens::Screen::Reader);
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
        vQueueDelete(jobQueue_);
        jobQueue_ = nullptr;
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
    JobUpdate complete;
    switch (jobKind_) {
    case JobKind::Rss: {
        Logger::checkpoint("rss_update");
        Preferences preferences;
        RssFeeds::Result result;
        if (!preferences.begin(settings::kStateNvsNamespace)) {
            result.summary = "RSS failed";
            result.detail = "Could not open state";
        } else {
            result = RssFeeds::check(preferences, settingsStore_.settings(), settingsStore_.secrets(),
                                     &App::renderStorageStatus, this);
            preferences.end();
        }
        storage_.refreshBooks();
        copyText(complete.line1, result.summary.c_str());
        copyText(complete.line2, result.detail.c_str());
        break;
    }
    case JobKind::StorageCheck: {
        Logger::checkpoint("storage_check");
        const StorageMigration::Report report =
            StorageMigration::repair(storage_.mounted(), {
                                                            .libraryItems = storage_.books().size(),
                                                            .fonts = readerScreen_.fonts.families().size() - 1,
                                                            .themes = interfaceScreen_.themes.themes().size() - 1,
                                                        });
        if (storage_.mounted()) {
            storage_.refreshBooks();
            readerScreen_.fonts.loadFromSd();
            interfaceScreen_.themes.loadFromSd();
            localeCatalog_ =
                locales::scanInstalled(Board::Storage::filesystem(), static_cast<size_t>(UiText::Count));
        }
        const std::string resultDetail = report.issues.empty()
                                           ? "Checked " + std::to_string(report.checked) + ", moved "
                                                 + std::to_string(report.moved) + ", cleaned "
                                                 + std::to_string(report.removed)
                                           : report.issues.front();
        copyText(complete.line1,
                 report.healthy ? report.diagnosticSummary.c_str() : "Storage needs attention");
        copyText(complete.line2, resultDetail.c_str());
        break;
    }
    case JobKind::OtaCheck: {
        Logger::checkpoint("ota_check");
        const OtaUpdater::Result result =
            OtaUpdater::checkOnly(settingsStore_.settings(), settingsStore_.secrets(), &App::renderStorageStatus, this);
        copyText(complete.line1, result.summary.c_str());
        copyText(complete.line2, result.detail.c_str());
        break;
    }
    case JobKind::OtaInstall: {
        Logger::checkpoint("ota_install");
        const OtaUpdater::Result result =
            OtaUpdater::checkAndInstall(settingsStore_.settings(), settingsStore_.secrets(), &App::renderStorageStatus,
                                        this);
        copyText(complete.line1, result.summary.c_str());
        copyText(complete.line2, result.detail.c_str());
        complete.rebootRequired = result.rebootRequired;
        break;
    }
    case JobKind::Book: {
        Logger::checkpoint("book_open");
        jobBookLoaded_ = storage_.loadIndexedBook(jobBookIndex_, readerScreen_.store, readerScreen_.session.metadata);
        break;
    }
    case JobKind::Typography:
        Logger::checkpoint("typography");
        readerScreen_.refreshTypography(settingsStore_.settings().reading, readerScreen_.session.state.overrides);
        break;
    case JobKind::None:
        break;
    }

    complete.complete = true;
    enqueueJobUpdate(complete, true);
}

bool App::requestTypographyRefresh() {
    if (typographyJobActive()) {
        typographyRefreshPending_ = true;
        return true;
    }
    if (backgroundJobActive())
        return false;

    if (startBackgroundJob(JobKind::Typography))
        return true;

    ESP_LOGW("reader", "typography task unavailable; preparing synchronously");
    readerScreen_.refreshTypography(settingsStore_.settings().reading, readerScreen_.session.state.overrides);
    return false;
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
    if (!startBackgroundJob(JobKind::Rss))
        showTransientStatus("RSS", immediateUi_.text(UiText::CouldNotStart), {}, 1200, screens::Screen::Reader);
}

void App::runBookOpen(size_t index, uint32_t nowMs) {
    if (!storage_.mounted() || index >= storage_.books().size())
        return;
    const BookLibrary::Entry& book = storage_.books()[index];
    if (typographyJobActive()) {
        pendingBookIndex_ = index;
        bookOpenPending_ = true;
        screens::status(immediateUi_, immediateUi_.text(UiText::OpeningBook), BookLibrary::displayName(book), {}, 5);
        screen_ = screens::Screen::Status;
        statusUntilMs_ = 0;
        return;
    }
    if (backgroundJobActive())
        return;

    jobBookIndex_ = index;
    jobBookLoaded_ = false;
    screens::status(immediateUi_, immediateUi_.text(UiText::OpeningBook), BookLibrary::displayName(book), {}, 5);
    screen_ = screens::Screen::Status;
    statusUntilMs_ = 0;
    readerScreen_.prepareBookOpen(prefs_, nowMs);
    if (!startBackgroundJob(JobKind::Book))
        showTransientStatus(immediateUi_.text(UiText::BookFailed), BookLibrary::displayName(book),
                            immediateUi_.text(UiText::CheckSdCard), 1200, screens::Screen::Library);
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
    if (!startBackgroundJob(install ? JobKind::OtaInstall : JobKind::OtaCheck))
        showTransientStatus("OTA", immediateUi_.text(UiText::CouldNotStart), {}, 1200, screens::Screen::Reader);
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
