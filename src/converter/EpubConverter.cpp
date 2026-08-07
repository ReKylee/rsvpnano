#include "converter/EpubConverter.h"
#include <esp_log.h>

#include <algorithm>
#include <iterator>
#include "board/BoardStorage.h"

#include "converter/EpubPackage.h"
#include "converter/EpubZip.h"
#include "text/LocaleTag.h"
#include "storage/fs/StoragePaths.h"
#include "text/AsciiText.h"
#include "text/TextNormalizer.h"

namespace {

    constexpr size_t kMaxOpfBytes = 256UL * 1024UL;
    constexpr size_t kMaxTocBytes = 256UL * 1024UL;
    constexpr size_t kMaxContainerBytes = 32UL * 1024UL;
    constexpr const char* kConverterVersion = "stream-v9";

    using EpubPackage::basenameWithoutExtension;
    using EpubPackage::directoryForPath;
    using EpubPackage::findManifestItem;
    using EpubPackage::isContentDocument;
    using EpubPackage::ManifestItem;
    using EpubPackage::parseDcMetadata;
    using EpubPackage::parseManifestItems;
    using EpubPackage::parseNavTocEntries;
    using EpubPackage::parseNcxTocEntries;
    using EpubPackage::parsePackageVersion;
    using EpubPackage::parseRootfilePath;
    using EpubPackage::parseSpineIds;
    using EpubPackage::TocEntry;
    using EpubPackage::toLowerCopy;

    struct PackageDocuments {
        std::string opfXml;
        std::string opfPath;
        std::string opfBaseDir;
    };

    struct ConversionPaths {
        std::string temp;
        std::string failed;
        std::string lock;
    };

    void serviceBackground() {
        yield();
        delay(0);
    }

    void reportProgress(const EpubConverter::Options& options, const char* line1, const char* line2,
                        int progressPercent) {
        if (options.progressCallback == nullptr) {
            return;
        }

        progressPercent = std::max(0, std::min(100, progressPercent));
        options.progressCallback(options, line1, line2, progressPercent);
        serviceBackground();
    }

    std::string wordCountDetail(size_t wordCount) {
        return std::to_string(wordCount) + " words";
    }

    std::string itemProgressDetail(size_t itemIndex, size_t itemCount, size_t wordCount) {
        return std::to_string(itemIndex + 1) + "/" + std::to_string(itemCount) + " " + std::to_string(wordCount)
             + " words";
    }

    int contentProgressPercent(size_t completedItems, size_t itemCount) {
        return 25 + static_cast<int>((completedItems * 70UL) / itemCount);
    }

    bool readPackageDocuments(EpubZip::Archive& zip, const EpubConverter::Options& options,
                              PackageDocuments& documents) {
        std::string containerXml;

        reportProgress(options, "Opening EPUB", "Reading metadata", 8);
        ESP_LOGD("epub", "Reading META-INF/container.xml");
        if (!zip.extractToString("META-INF/container.xml", containerXml, kMaxContainerBytes)) {
            ESP_LOGE("epub", "EPUB container.xml not found or unreadable");
            return false;
        }
        ESP_LOGI("epub", "container.xml loaded: %u chars", static_cast<unsigned int>(containerXml.length()));

        documents.opfPath = parseRootfilePath(containerXml);
        if (documents.opfPath.empty()) {
            ESP_LOGE("epub", "EPUB rootfile path not found");
            return false;
        }
        ESP_LOGD("epub", "Rootfile OPF path: %s", documents.opfPath.c_str());

        reportProgress(options, "Opening EPUB", "Reading package", 14);
        ESP_LOGD("epub", "Reading OPF package: %s", documents.opfPath.c_str());
        if (!zip.extractToString(documents.opfPath, documents.opfXml, kMaxOpfBytes)) {
            ESP_LOGE("epub", "OPF file not readable: %s", documents.opfPath.c_str());
            return false;
        }

        ESP_LOGI("epub", "OPF loaded: %u chars", static_cast<unsigned int>(documents.opfXml.length()));
        documents.opfBaseDir = directoryForPath(documents.opfPath);
        return true;
    }

    std::vector<std::string> contentDocumentsInSpineOrder(const std::vector<ManifestItem>& manifest,
                                                          const std::vector<std::string>& spineIds) {
        std::vector<std::string> order;
        order.reserve(spineIds.size());

        std::ranges::for_each(spineIds, [&](const std::string& spineId) {
            serviceBackground();
            const ManifestItem* item = findManifestItem(manifest, spineId);
            if (item != nullptr && isContentDocument(*item)) {
                order.push_back(item->path);
            }
        });

        return order;
    }

    std::vector<std::string> allContentDocuments(const std::vector<ManifestItem>& manifest) {
        std::vector<std::string> order;
        order.reserve(manifest.size());
        std::ranges::for_each(manifest, [&](const ManifestItem& item) {
            if (isContentDocument(item)) {
                order.push_back(item.path);
            }
        });
        return order;
    }

    std::vector<std::string> buildReadingOrder(std::string_view opfXml, std::string_view opfBaseDir,
                                               const EpubConverter::Options& options) {
        const std::vector<ManifestItem> manifest = parseManifestItems(opfXml, opfBaseDir);
        const std::vector<std::string> spineIds = parseSpineIds(opfXml);

        ESP_LOGD("epub", "Package parsed: manifest=%u spine=%u base=%.*s", static_cast<unsigned int>(manifest.size()),
                 static_cast<unsigned int>(spineIds.size()), static_cast<int>(opfBaseDir.size()), opfBaseDir.data());

        reportProgress(options, "Opening EPUB", "Building reading order", 20);
        return [&]() {
            std::vector<std::string> order = contentDocumentsInSpineOrder(manifest, spineIds);
            return order.empty() ? allContentDocuments(manifest) : order;
        }();
    }

    bool hasProperty(std::string_view properties, std::string_view wanted) {
        size_t position = 0;
        while (position < properties.length()) {
            while (position < properties.length() && AsciiText::isWhitespace(properties[position])) {
                ++position;
            }
            size_t end = position;
            while (end < properties.length() && !AsciiText::isWhitespace(properties[end])) {
                ++end;
            }
            if (properties.substr(position, end - position) == wanted) {
                return true;
            }
            position = end;
        }
        return false;
    }

    std::vector<TocEntry> firstReadableToc(EpubZip::Archive& zip, const std::vector<const ManifestItem*>& items,
                                           std::string_view bookTitle, bool navDocument) {
        for (const ManifestItem* item: items) {
            std::string markup;
            if (!zip.extractToString(item->path, markup, kMaxTocBytes)) {
                continue;
            }
            std::vector<TocEntry> entries = navDocument ? parseNavTocEntries(markup, item->path, bookTitle)
                                                        : parseNcxTocEntries(markup, item->path, bookTitle);
            if (!entries.empty()) {
                return entries;
            }
        }
        return {};
    }

    std::vector<TocEntry> readToc(EpubZip::Archive& zip, std::string_view opfXml, std::string_view opfBaseDir,
                                  std::string_view bookTitle) {
        const std::vector<ManifestItem> manifest = parseManifestItems(opfXml, opfBaseDir);
        std::vector<const ManifestItem*> navDocuments;
        std::vector<const ManifestItem*> ncxDocuments;

        for (const ManifestItem& item: manifest) {
            if (hasProperty(item.properties, "nav")) {
                navDocuments.push_back(&item);
            }
            if (toLowerCopy(item.mediaType) == "application/x-dtbncx+xml") {
                ncxDocuments.push_back(&item);
            }
        }

        const bool epub3 = parsePackageVersion(opfXml).starts_with('3');
        std::vector<TocEntry> entries = epub3 ? firstReadableToc(zip, navDocuments, bookTitle, true)
                                              : firstReadableToc(zip, ncxDocuments, bookTitle, false);
        if (!entries.empty()) {
            return entries;
        }
        return epub3 ? firstReadableToc(zip, ncxDocuments, bookTitle, false)
                     : firstReadableToc(zip, navDocuments, bookTitle, true);
    }

    std::string fallbackChapterTitle(std::string_view path) {
        std::string title = basenameWithoutExtension(path);
        std::ranges::replace(title, '_', ' ');
        std::ranges::replace(title, '-', ' ');
        title = std::string{AsciiText::trim(title)};
        if (!title.empty() && title[0] >= 'a' && title[0] <= 'z') {
            title[0] = static_cast<char>(title[0] - ('a' - 'A'));
        }
        return title;
    }

    void writeRsvpHeader(File& output, std::string_view epubPath, std::string_view opfXml) {
        const std::string title = [&]() {
            const std::string metadataTitle = parseDcMetadata(opfXml, "title");
            return metadataTitle.empty() ? basenameWithoutExtension(epubPath) : metadataTitle;
        }();
        const std::string author = parseDcMetadata(opfXml, "creator");
        const auto locale = LocaleTag::normalize(parseDcMetadata(opfXml, "language"));

        output.println("@rsvp 1");
        output.print("@title ");
        output.println(RsvpText::normalizeDisplayText(title).c_str());
        if (!author.empty()) {
            output.print("@author ");
            output.println(RsvpText::normalizeDisplayText(author).c_str());
        }
        if (locale) {
            output.print("@language ");
            output.println(locale->c_str());
        }
        output.print("@source ");
        output.println(RsvpText::normalizeDisplayText(epubPath).c_str());
        output.print("@converter ");
        output.println(kConverterVersion);
        output.println();
    }

    void reportReadingOrderReady(const EpubConverter::Options& options, const std::vector<std::string>& readingOrder) {
        ESP_LOGD("epub", "Reading order contains %u content files", static_cast<unsigned int>(readingOrder.size()));
        const std::string foundDetail = std::to_string(readingOrder.size()) + " content files";
        reportProgress(options, "Opening EPUB", foundDetail.c_str(), 25);
    }

    void streamReadingOrder(EpubZip::Archive& zip, File& output, const std::vector<std::string>& readingOrder,
                            const std::vector<TocEntry>& tocEntries, std::string_view bookTitle,
                            std::string_view bookLocale,
                            const EpubConverter::Options& options, size_t& wordCount, size_t& chapterCount) {
        std::string lastChapterTitle;
        const bool hasToc = !tocEntries.empty();

        const auto withinWordLimit = [&]() {
            return options.maxWords == 0 || wordCount < options.maxWords;
        };

        const auto reportItemProgress = [&](const char* title, size_t itemIndex) {
            const std::string detail = itemProgressDetail(itemIndex, readingOrder.size(), wordCount);
            reportProgress(options, title, detail.c_str(), contentProgressPercent(itemIndex, readingOrder.size()));
        };

        for (size_t i = 0; i < readingOrder.size() && withinWordLimit(); ++i) {
            serviceBackground();

            reportItemProgress("Extracting content", i);

            std::vector<TocEntry> documentTocEntries;
            const std::string loweredPath = toLowerCopy(readingOrder[i]);
            std::copy_if(tocEntries.begin(), tocEntries.end(), std::back_inserter(documentTocEntries),
                         [&](const TocEntry& entry) {
                             return toLowerCopy(entry.path) == loweredPath;
                         });

            const EpubZip::ContentExtractStatus extractStatus =
                zip.extractContentToRsvp(readingOrder[i], output, wordCount, options.maxWords, lastChapterTitle,
                                         chapterCount, documentTocEntries, hasToc,
                                         fallbackChapterTitle(readingOrder[i]), bookTitle, bookLocale, options, i,
                                         readingOrder.size());

            reportItemProgress("Parsed content", i + 1);

            if (extractStatus == EpubZip::ContentExtractStatus::Unsupported
                || extractStatus == EpubZip::ContentExtractStatus::Failed) {
                ESP_LOGE("epub", "Skipping unreadable content file: %s", readingOrder[i].c_str());
                continue;
            }

            if (extractStatus == EpubZip::ContentExtractStatus::WordLimitReached) {
                break;
            }
        }
    }

    bool promoteTempFile(std::string_view tempPath, std::string_view rsvpPath) {
        const std::string temp{tempPath};
        const std::string destination{rsvpPath};
        Board::Storage::filesystem().remove(destination.c_str());
        if (Board::Storage::filesystem().rename(temp.c_str(), destination.c_str())) {
            return true;
        }

        ESP_LOGE("epub", "Could not rename %s to %s", temp.c_str(), destination.c_str());
        Board::Storage::filesystem().remove(temp.c_str());
        return false;
    }

    bool convertEpubToRsvp(std::string_view epubPath, std::string_view tempPath, std::string_view rsvpPath,
                           const EpubConverter::Options& options) {
        const std::string sourcePath{epubPath};
        const std::string temporaryPath{tempPath};
        reportProgress(options, "Opening EPUB", "Reading archive", 0);

        EpubZip::Archive zip;
        if (!zip.open(epubPath)) {
            ESP_LOGE("epub", "Could not open EPUB archive: %s", sourcePath.c_str());
            return false;
        }

        if (zip.contains("META-INF/encryption.xml")) {
            ESP_LOGW("epub", "Encrypted EPUB content is unsupported");
            zip.close();
            return false;
        }

        const auto failWithClosedZip = [&]() {
            zip.close();
            return false;
        };

        PackageDocuments documents;
        if (!readPackageDocuments(zip, options, documents)) {
            return failWithClosedZip();
        }

        const std::vector<std::string> readingOrder =
            buildReadingOrder(documents.opfXml, documents.opfBaseDir, options);
        if (readingOrder.empty()) {
            ESP_LOGD("epub", "No readable XHTML spine items found");
            return failWithClosedZip();
        }
        reportReadingOrderReady(options, readingOrder);

        const std::string bookTitle = [&]() {
            const std::string metadataTitle = parseDcMetadata(documents.opfXml, "title");
            return metadataTitle.empty() ? basenameWithoutExtension(epubPath) : metadataTitle;
        }();
        const auto normalizedBookLocale = LocaleTag::normalize(parseDcMetadata(documents.opfXml, "language"));
        const std::string bookLocale = normalizedBookLocale ? std::move(*normalizedBookLocale) : "und";
        const std::vector<TocEntry> tocEntries = readToc(zip, documents.opfXml, documents.opfBaseDir, bookTitle);
        ESP_LOGD("epub", "Usable TOC entries: %u", static_cast<unsigned int>(tocEntries.size()));

        Board::Storage::filesystem().remove(temporaryPath.c_str());
        File output = Board::Storage::filesystem().open(temporaryPath.c_str(), FILE_WRITE);
        if (!output) {
            ESP_LOGE("epub", "Could not create temporary RSVP file: %s", temporaryPath.c_str());
            return failWithClosedZip();
        }

        writeRsvpHeader(output, epubPath, documents.opfXml);

        size_t wordCount = 0;
        size_t chapterCount = 0;
        streamReadingOrder(zip, output, readingOrder, tocEntries, bookTitle, bookLocale, options, wordCount,
                           chapterCount);

        const std::string finishingDetail = wordCountDetail(wordCount);
        reportProgress(options, "Finishing EPUB", finishingDetail.c_str(), 96);
        output.close();
        zip.close();

        if (wordCount == 0) {
            ESP_LOGD("epub", "No readable words extracted from %s", sourcePath.c_str());
            Board::Storage::filesystem().remove(temporaryPath.c_str());
            return false;
        }

        if (!promoteTempFile(tempPath, rsvpPath)) {
            return false;
        }

        ESP_LOGI("epub", "Converted %.*s -> %.*s (%u words)", static_cast<int>(epubPath.size()), epubPath.data(),
                 static_cast<int>(rsvpPath.size()), rsvpPath.data(), static_cast<unsigned int>(wordCount));
        const std::string convertedDetail = wordCountDetail(wordCount);
        reportProgress(options, "EPUB converted", convertedDetail.c_str(), 100);
        return true;
    }

    void writeFailureMarker(std::string_view markerPath, const char* message) {
        const std::string path{markerPath};
        Board::Storage::filesystem().remove(path.c_str());

        File marker = Board::Storage::filesystem().open(path.c_str(), FILE_WRITE);
        if (!marker) {
            ESP_LOGE("epub", "Could not create failure marker: %s", path.c_str());
            return;
        }

        marker.println(message == nullptr ? "Conversion failed" : message);
        marker.print("converter=");
        marker.println(kConverterVersion);
        marker.close();
    }

    bool markerWasWrittenByCurrentConverter(File& marker) {
        std::string content;
        content.reserve(256);
        while (marker.available() && content.length() < 256) {
            content += static_cast<char>(marker.read());
        }

        const std::string expected = std::string("converter=") + kConverterVersion;
        return content.contains(expected);
    }

    bool rsvpWasWrittenByCurrentConverter(File& file) {
        if (!file || file.isDirectory()) {
            return false;
        }

        file.seek(0);
        std::string line;
        line.reserve(128);
        size_t scannedLines = 0;
        while (file.available() && scannedLines < 12) {
            const char c = static_cast<char>(file.read());
            if (c == '\r') {
                continue;
            }
            if (c == '\n') {
                line = std::string{AsciiText::trim(line)};
                if (line.starts_with("@converter")) {
                    const std::string expected = std::string("@converter ") + kConverterVersion;
                    return line == expected;
                }
                if (!line.empty() && !line.starts_with('@')) {
                    break;
                }
                line.clear();
                ++scannedLines;
                continue;
            }
            if (line.length() < 128) {
                line += c;
            }
        }

        line = std::string{AsciiText::trim(line)};
        if (line.starts_with("@converter")) {
            const std::string expected = std::string("@converter ") + kConverterVersion;
            return line == expected;
        }

        return false;
    }

    ConversionPaths conversionPathsFor(std::string_view rsvpPath) {
        return {
            std::string{rsvpPath} + StoragePaths::kTempExtension,
            std::string{rsvpPath} + StoragePaths::kFailedExtension,
            std::string{rsvpPath} + StoragePaths::kConvertingExtension,
        };
    }

    bool removeStaleCacheOrReuseCurrent(std::string_view rsvpPath) {
        const std::string path{rsvpPath};
        File existing = Board::Storage::filesystem().open(path.c_str());
        if (!existing) {
            return false;
        }

        const bool hasCache = !existing.isDirectory() && existing.size() > 0;
        const bool currentCache = hasCache && rsvpWasWrittenByCurrentConverter(existing);
        existing.close();

        if (!hasCache) {
            return false;
        }
        if (currentCache) {
            return true;
        }

        ESP_LOGW("epub", "Rebuilding stale RSVP cache after converter update: %s", path.c_str());
        Board::Storage::filesystem().remove(path.c_str());
        return false;
    }

    bool previousCurrentAttemptRestarted(std::string_view epubPath, const ConversionPaths& paths,
                                         const EpubConverter::Options& options) {
        File lock = Board::Storage::filesystem().open(paths.lock.c_str());
        if (!lock) {
            return false;
        }

        const bool lockMarker = !lock.isDirectory();
        const bool currentLock = lockMarker && markerWasWrittenByCurrentConverter(lock);
        lock.close();

        if (!lockMarker) {
            return false;
        }

        Board::Storage::filesystem().remove(paths.lock.c_str());
        Board::Storage::filesystem().remove(paths.temp.c_str());
        if (!currentLock) {
            ESP_LOGW("epub", "Retrying interrupted EPUB after converter update: %.*s",
                     static_cast<int>(epubPath.size()), epubPath.data());
            return false;
        }

        ESP_LOGW("epub", "Previous conversion restart detected, skipping: %.*s", static_cast<int>(epubPath.size()),
                 epubPath.data());
        writeFailureMarker(paths.failed, "Previous conversion restarted before completion.");
        reportProgress(options, "Previous restart", "Skipping this EPUB", 100);
        return true;
    }

    void removeOrphanedTempFile(std::string_view epubPath, std::string_view tempPath) {
        const std::string path{tempPath};
        File temp = Board::Storage::filesystem().open(path.c_str());
        if (!temp) {
            return;
        }

        const bool interruptedTemp = !temp.isDirectory();
        temp.close();
        if (!interruptedTemp) {
            return;
        }

        ESP_LOGW("epub", "Removing stale temporary conversion file and retrying: %.*s",
                 static_cast<int>(epubPath.size()), epubPath.data());
        Board::Storage::filesystem().remove(path.c_str());
    }

    bool shouldSkipCurrentFailure(std::string_view epubPath, std::string_view failedPath) {
        const std::string path{failedPath};
        File failed = Board::Storage::filesystem().open(path.c_str());
        if (!failed) {
            return false;
        }

        const bool failedMarker = !failed.isDirectory();
        const bool currentFailure = failedMarker && markerWasWrittenByCurrentConverter(failed);
        failed.close();

        if (!failedMarker) {
            return false;
        }
        if (currentFailure) {
            ESP_LOGW("epub", "Skipping EPUB with failure marker: %.*s", static_cast<int>(epubPath.size()),
                     epubPath.data());
            return true;
        }

        ESP_LOGW("epub", "Retrying EPUB after converter update: %.*s", static_cast<int>(epubPath.size()),
                 epubPath.data());
        Board::Storage::filesystem().remove(path.c_str());
        return false;
    }

} // namespace

bool EpubConverter::isCurrentCache(std::string_view rsvpPath) {
    const std::string path{rsvpPath};
    File existing = Board::Storage::filesystem().open(path.c_str());
    const bool current = rsvpWasWrittenByCurrentConverter(existing);
    if (existing) {
        existing.close();
    }
    return current;
}

std::expected<void, std::error_code> EpubConverter::convertIfNeeded(std::string_view epubPath,
                                                                    std::string_view rsvpPath, const Options& options) {
    if (removeStaleCacheOrReuseCurrent(rsvpPath))
        return {};

    const ConversionPaths paths = conversionPathsFor(rsvpPath);
    if (previousCurrentAttemptRestarted(epubPath, paths, options))
        return std::unexpected(std::make_error_code(std::errc::operation_canceled));

    removeOrphanedTempFile(epubPath, paths.temp);
    if (shouldSkipCurrentFailure(epubPath, paths.failed))
        return std::unexpected(std::make_error_code(std::errc::resource_unavailable_try_again));

    ESP_LOGD("epub", "Converting on device: %.*s", static_cast<int>(epubPath.size()), epubPath.data());
    writeFailureMarker(paths.lock, "Conversion in progress. Delete this file only if retrying.");
    const bool converted = convertEpubToRsvp(epubPath, paths.temp, rsvpPath, options);
    Board::Storage::filesystem().remove(paths.lock.c_str());
    if (!converted) {
        writeFailureMarker(paths.failed, "Conversion failed. Remove this marker to retry.");
        return std::unexpected(std::make_error_code(std::errc::io_error));
    }

    Board::Storage::filesystem().remove(paths.failed.c_str());
    return {};
}
