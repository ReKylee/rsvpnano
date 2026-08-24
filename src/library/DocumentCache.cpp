#include "library/DocumentCache.h"

#include <esp_log.h>

#include <algorithm>
#include <string>

#include "board/BoardStorage.h"
#include "conversion/epub/EpubConverter.h"
#include "conversion/pdf/PdfConverter.h"
#include "storage/fs/StorageFiles.h"
#include "storage/fs/StoragePaths.h"
#include "text/AsciiText.h"
#include "text/RsvpTokenizer.h"

#ifndef RSVP_ON_DEVICE_DOCUMENT_CONVERSION
#define RSVP_ON_DEVICE_DOCUMENT_CONVERSION 0
#endif

#if RSVP_ON_DEVICE_DOCUMENT_CONVERSION
#include <esp_heap_caps.h>
#endif

namespace DocumentCache {
    namespace {

        using namespace StoragePaths;

        struct CachePaths {
            std::string temporary;
            std::string failed;
            std::string converting;
        };

        std::string_view formatName(std::string_view path) {
            return hasPdfExtension(path) ? "PDF" : "EPUB";
        }

        std::string_view converterVersion(std::string_view path) {
            return hasPdfExtension(path) ? PdfConverter::kVersion : EpubConverter::kVersion;
        }

#if RSVP_ON_DEVICE_DOCUMENT_CONVERSION
        struct ProgressContext {
            std::string_view label;
        };

        void logHeap(const char* label) {
            ESP_LOGD("heap", "%s free8=%lu free_spiram=%lu largest8=%lu largest_spiram=%lu",
                     label == nullptr ? "" : label,
                     static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
                     static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
                     static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
                     static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
        }

        void logConversionProgress(void* context, const char* line1, const char* line2, int progressPercent) {
            const auto& progress = *static_cast<const ProgressContext*>(context);
            ESP_LOGD("document-progress", "%d%% %.*s | %s | %s", std::clamp(progressPercent, 0, 100),
                     static_cast<int>(progress.label.size()), progress.label.data(), line1 == nullptr ? "" : line1,
                     line2 == nullptr ? "" : line2);
            yield();
            delay(0);
        }

        std::expected<void, std::error_code> convertDocument(std::string_view documentPath,
                                                              std::string_view rsvpPath,
                                                              std::string_view label) {
            ProgressContext progress{.label = label};
            if (hasEpubExtension(documentPath)) {
                EpubConverter::Options options;
                options.conversion.maxWords = RsvpText::kMaxBookWords;
                options.conversion.progressCallback = logConversionProgress;
                options.conversion.progressContext = &progress;
                return EpubConverter::convert(documentPath, rsvpPath, options);
            }

            PdfConverter::Options options;
            options.maxWords = RsvpText::kMaxBookWords;
            options.progressCallback = logConversionProgress;
            options.progressContext = &progress;
            return PdfConverter::convert(documentPath, rsvpPath, options);
        }
#endif

        bool fileContainsConverter(File& file, std::string_view converter) {
            if (!file || file.isDirectory())
                return false;
            file.seek(0);
            std::string line;
            line.reserve(128);
            for (size_t lines = 0; file.available() && lines < 12;) {
                const char c = static_cast<char>(file.read());
                if (c == '\r')
                    continue;
                if (c != '\n') {
                    if (line.size() < 128)
                        line += c;
                    continue;
                }
                line = std::string{AsciiText::trim(line)};
                if (line.starts_with("@converter "))
                    return std::string_view{line}.substr(11) == converter;
                if (!line.empty() && !line.starts_with('@'))
                    return false;
                line.clear();
                ++lines;
            }
            return line.starts_with("@converter ") && std::string_view{line}.substr(11) == converter;
        }

        bool markerIsCurrent(std::string_view path, std::string_view converter) {
            const std::string ownedPath{path};
            File file = Board::Storage::filesystem().open(ownedPath.c_str());
            if (!file || file.isDirectory()) {
                if (file)
                    file.close();
                return false;
            }
            std::string content;
            content.reserve(128);
            while (file.available() && content.size() < 128)
                content += static_cast<char>(file.read());
            file.close();
            return content.contains(std::string{"converter="} + std::string{converter});
        }

        void writeMarker(std::string_view path, std::string_view message, std::string_view converter) {
            const std::string ownedPath{path};
            Board::Storage::filesystem().remove(ownedPath.c_str());
            File marker = Board::Storage::filesystem().open(ownedPath.c_str(), FILE_WRITE);
            if (!marker)
                return;
            marker.println(std::string{message}.c_str());
            marker.print("converter=");
            marker.println(std::string{converter}.c_str());
            marker.close();
        }

        CachePaths cachePaths(std::string_view rsvpPath) {
            return {
                std::string{rsvpPath} + kTempExtension,
                std::string{rsvpPath} + kFailedExtension,
                std::string{rsvpPath} + kConvertingExtension,
            };
        }

        bool promote(std::string_view temporaryPath, std::string_view rsvpPath) {
            const std::string temporary{temporaryPath};
            const std::string destination{rsvpPath};
            Board::Storage::filesystem().remove(destination.c_str());
            if (Board::Storage::filesystem().rename(temporary.c_str(), destination.c_str()))
                return true;
            Board::Storage::filesystem().remove(temporary.c_str());
            return false;
        }

    } // namespace

    bool rsvpIsCurrent(std::string_view documentPath, std::string_view rsvpPath) {
        const std::string path{rsvpPath};
        File file = Board::Storage::filesystem().open(path.c_str());
        const bool current = fileContainsConverter(file, converterVersion(documentPath));
        if (file)
            file.close();
        return current;
    }

    bool hasCurrentCache(std::string_view documentPath) {
        return rsvpIsCurrent(documentPath, rsvpCachePathForDocument(documentPath));
    }

    std::string libraryLabel(std::string_view documentPath) {
        const std::string rsvpPath = rsvpCachePathForDocument(documentPath);
        const std::string_view format = formatName(documentPath);
        if (StorageFiles::fileExists((rsvpPath + kFailedExtension).c_str()))
            return std::string{format} + " failed - check serial";
        if (StorageFiles::fileExists((rsvpPath + kConvertingExtension).c_str())
            || StorageFiles::fileExists((rsvpPath + kTempExtension).c_str()))
            return std::string{format} + " interrupted";
        return std::string{format} + " - converts on open";
    }

    std::expected<std::string, std::error_code> ensureConverted(std::string_view documentPathView,
                                                                StatusCallback statusCallback, void* statusContext) {
        const std::string documentPath{documentPathView};
        if (!hasConvertibleDocumentExtension(documentPath))
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        const std::string displayName = displayNameForPath(documentPath);
        const std::string_view format = formatName(documentPath);
        auto report = [&](const char* title, const char* line1 = "", const char* line2 = "", int progress = -1) {
            if (statusCallback != nullptr)
                statusCallback(statusContext, title, line1, line2, progress);
        };

#if !RSVP_ON_DEVICE_DOCUMENT_CONVERSION
        ESP_LOGD("storage", "%.*s conversion disabled at build time: %s", static_cast<int>(format.size()),
                 format.data(), documentPath.c_str());
        report("Document unsupported", displayName.c_str(), "Build flag is disabled", 100);
        return std::unexpected(std::make_error_code(std::errc::not_supported));
#else
        const std::string rsvpPath = rsvpCachePathForDocument(documentPath);
        const std::string_view version = converterVersion(documentPath);
        if (!StorageFiles::fileExistsWithBytes(documentPath.c_str())) {
            report("Preparing book", displayName.c_str(), "Source file missing", 100);
            return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
        }
        if (hasCurrentCache(documentPath))
            return rsvpPath;

        const CachePaths paths = cachePaths(rsvpPath);
        if (markerIsCurrent(paths.failed, version)) {
            report("Preparing book", displayName.c_str(), "Previous conversion failed", 100);
            return std::unexpected(std::make_error_code(std::errc::resource_unavailable_try_again));
        }
        Board::Storage::filesystem().remove(paths.failed.c_str());

        if (markerIsCurrent(paths.converting, version)) {
            Board::Storage::filesystem().remove(paths.converting.c_str());
            Board::Storage::filesystem().remove(paths.temporary.c_str());
            writeMarker(paths.failed, "Previous conversion restarted before completion.", version);
            report("Preparing book", displayName.c_str(), "Previous conversion was interrupted", 100);
            return std::unexpected(std::make_error_code(std::errc::operation_canceled));
        }
        Board::Storage::filesystem().remove(paths.converting.c_str());
        Board::Storage::filesystem().remove(paths.temporary.c_str());
        Board::Storage::filesystem().remove(rsvpPath.c_str());

        size_t sourceBytes = 0;
        File source = Board::Storage::filesystem().open(documentPath.c_str());
        if (source) {
            sourceBytes = static_cast<size_t>(source.size());
            source.close();
        }
        ESP_LOGD("storage", "Preparing %.*s conversion: source=%s output=%s size=%lu bytes",
                 static_cast<int>(format.size()), format.data(), documentPath.c_str(), rsvpPath.c_str(),
                 static_cast<unsigned long>(sourceBytes));
        logHeap("before document conversion");
        report("Preparing book", displayName.c_str(), "Converting document", 0);

        writeMarker(paths.converting, "Conversion in progress.", version);
        const uint32_t started = millis();
        const auto converted = convertDocument(documentPath, paths.temporary, displayName);
        const uint32_t elapsed = millis() - started;
        Board::Storage::filesystem().remove(paths.converting.c_str());
        logHeap("after document conversion");

        if (!converted || !StorageFiles::fileExistsWithBytes(paths.temporary.c_str())
            || !promote(paths.temporary, rsvpPath)) {
            Board::Storage::filesystem().remove(paths.temporary.c_str());
            writeMarker(paths.failed, "Conversion failed. Remove this marker to retry.", version);
            ESP_LOGE("storage", "%.*s conversion failed after %lu ms: %s", static_cast<int>(format.size()),
                     format.data(), static_cast<unsigned long>(elapsed), documentPath.c_str());
            report("Preparing book", displayName.c_str(), "Document conversion failed", 100);
            return std::unexpected(converted ? std::make_error_code(std::errc::io_error) : converted.error());
        }

        Board::Storage::filesystem().remove(paths.failed.c_str());
        ESP_LOGI("storage", "%.*s conversion ready after %lu ms: %s", static_cast<int>(format.size()), format.data(),
                 static_cast<unsigned long>(elapsed), rsvpPath.c_str());
        report("Preparing book", displayName.c_str(), "Conversion complete", 100);
        return rsvpPath;
#endif
    }

} // namespace DocumentCache
