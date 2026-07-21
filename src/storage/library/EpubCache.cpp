#include "storage/library/EpubCache.h"
#include <esp_log.h>

#include <algorithm>
#include <esp_heap_caps.h>
#include "board/BoardStorage.h"

#include "converter/EpubConverter.h"
#include "storage/fs/StorageFiles.h"
#include "storage/fs/StoragePaths.h"
#include "text/RsvpTokenizer.h"

#ifndef RSVP_ON_DEVICE_EPUB_CONVERSION
#define RSVP_ON_DEVICE_EPUB_CONVERSION 0
#endif

namespace EpubCache {
    namespace {

        using namespace StoragePaths;

        void logHeapSnapshot(const char* label) {
            ESP_LOGD("heap", "%s free8=%lu free_spiram=%lu largest8=%lu largest_spiram=%lu",
                     label == nullptr ? "" : label,
                     static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
                     static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
                     static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
                     static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
        }

        void logConversionProgress(const EpubConverter::Options& options, const char* line1, const char* line2,
                                   int progressPercent) {
            const int overallPercent = std::max(0, std::min(100, progressPercent));
            const String detail = String(line1 == nullptr ? "" : line1) + " - " + String(line2 == nullptr ? "" : line2);
            const char* title = options.progressTitle.isEmpty() ? "EPUB" : options.progressTitle.c_str();
            ESP_LOGD("epub-progress", "%d%% %s | %s | %s", overallPercent, title, options.progressLabel.c_str(),
                     detail.c_str());

            // Keep the display on the static conversion screen while ZIP work owns the SD
            // archive.
            yield();
            delay(0);
        }

    } // namespace

    bool rsvpIsCurrent(const String& rsvpPath) {
        return StorageFiles::fileExistsWithBytes(rsvpPath.c_str()) && EpubConverter::isCurrentCache(rsvpPath);
    }

    bool hasCurrentCache(const String& epubPath) {
        return rsvpIsCurrent(StoragePaths::rsvpCachePathForEpub(epubPath));
    }

    String libraryLabel(const String& epubPath) {
        const String rsvpPath = StoragePaths::rsvpCachePathForEpub(epubPath);
        if (StorageFiles::fileExists((rsvpPath + StoragePaths::kFailedExtension).c_str())) {
            return "EPUB failed - check serial";
        }
        if (StorageFiles::fileExists((rsvpPath + StoragePaths::kConvertingExtension).c_str())
            || StorageFiles::fileExists((rsvpPath + StoragePaths::kTempExtension).c_str())) {
            return "EPUB interrupted";
        }
        return "EPUB - converts on open";
    }

    std::expected<String, std::error_code> ensureConverted(const String& epubPath, StatusCallback statusCallback,
                                                           void* statusContext) {
        const String rsvpPath = rsvpCachePathForEpub(epubPath);
        auto report = [&](const char* title, const char* line1 = "", const char* line2 = "", int progressPercent = -1) {
            if (statusCallback != nullptr) {
                statusCallback(statusContext, title, line1, line2, progressPercent);
            }
        };

        {
            // Cache checks and unsupported source handling.
            if (!RSVP_ON_DEVICE_EPUB_CONVERSION) {
                ESP_LOGD("storage", "EPUB conversion disabled at build time: %s", epubPath.c_str());
                report("EPUB unsupported", displayNameForPath(epubPath).c_str(), "Build flag is disabled", 100);
                return std::unexpected(std::make_error_code(std::errc::not_supported));
            }

            if (!StorageFiles::fileExistsWithBytes(epubPath.c_str())) {
                ESP_LOGW("storage", "EPUB source missing or empty: %s", epubPath.c_str());
                report("Preparing book", displayNameForPath(epubPath).c_str(), "EPUB missing", 100);
                return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
            }

            if (hasCurrentCache(epubPath)) {
                ESP_LOGD("storage", "EPUB cache hit: %s -> %s", epubPath.c_str(), rsvpPath.c_str());
                return rsvpPath;
            }

            if (StorageFiles::fileExistsWithBytes(rsvpPath.c_str())) {
                ESP_LOGW("storage", "EPUB cache stale after converter update: %s", rsvpPath.c_str());
            }
        }

        const size_t epubBytes = [&]() {
            // Read source size for conversion logging.
            File epubFile = Board::Storage::filesystem().open(epubPath);
            const size_t bytes = epubFile ? static_cast<size_t>(epubFile.size()) : 0;
            if (epubFile) {
                epubFile.close();
            }
            return bytes;
        }();

        ESP_LOGD("storage", "Preparing EPUB conversion: source=%s output=%s size=%lu bytes", epubPath.c_str(),
                 rsvpPath.c_str(), static_cast<unsigned long>(epubBytes));
        logHeapSnapshot("before EPUB conversion");
        report("Preparing book", displayNameForPath(epubPath).c_str(), "Converting EPUB", 0);

        uint32_t elapsedMs = 0;
        {
            // Run the converter with bounded word output and progress forwarding.
            EpubConverter::Options options;
            options.maxWords = RsvpText::kMaxBookWords;
            options.progressCallback = logConversionProgress;
            options.progressTitle = "Preparing book";
            options.progressLabel = displayNameForPath(epubPath);

            const uint32_t startedMs = millis();
            const auto converted = EpubConverter::convertIfNeeded(epubPath, rsvpPath, options);
            elapsedMs = millis() - startedMs;
            if (!converted) {
                ESP_LOGE("storage", "EPUB conversion failed after %lu ms: %s", static_cast<unsigned long>(elapsedMs),
                         epubPath.c_str());
                report("Preparing book", "EPUB conversion failed", "Check serial monitor", 100);
                logHeapSnapshot("after EPUB conversion");
                return std::unexpected(converted.error());
            }
        }
        logHeapSnapshot("after EPUB conversion");

        if (!StorageFiles::fileExistsWithBytes(rsvpPath.c_str())) {
            ESP_LOGE("storage", "EPUB conversion failed after %lu ms: %s", static_cast<unsigned long>(elapsedMs),
                     epubPath.c_str());
            report("Preparing book", "EPUB conversion failed", "Check serial monitor", 100);
            return std::unexpected(std::make_error_code(std::errc::io_error));
        }

        ESP_LOGI("storage", "EPUB conversion ready after %lu ms: %s", static_cast<unsigned long>(elapsedMs),
                 rsvpPath.c_str());
        report("Preparing book", displayNameForPath(rsvpPath).c_str(), "Conversion complete", 100);
        return rsvpPath;
    }

} // namespace EpubCache
