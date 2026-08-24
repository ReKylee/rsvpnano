#include "conversion/epub/EpubConverter.h"
#include <esp_log.h>

#include <algorithm>
#include <iterator>
#include "board/BoardStorage.h"

#include "conversion/epub/EpubPackage.h"
#include "conversion/rsvp/RsvpWriter.h"
#include "conversion/epub/EpubZip.h"
#include "text/AsciiText.h"
#include "text/LocaleTag.h"
#include "text/TextNormalizer.h"
#include "text/WritingMode.h"

namespace {

    constexpr size_t kMaxOpfBytes = 256UL * 1024UL;
    constexpr size_t kMaxTocBytes = 256UL * 1024UL;
    constexpr size_t kMaxContainerBytes = 32UL * 1024UL;
    constexpr size_t kMaxCssBytes = 256UL * 1024UL;

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

    void serviceBackground() {
        yield();
        delay(0);
    }

    void reportProgress(const EpubConverter::Options& options, const char* line1, const char* line2,
                        int progressPercent) {
        if (options.conversion.progressCallback == nullptr) {
            return;
        }

        progressPercent = std::max(0, std::min(100, progressPercent));
        options.conversion.progressCallback(options.conversion.progressContext, line1, line2, progressPercent);
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

    std::vector<std::string> buildReadingOrder(const std::vector<ManifestItem>& manifest, std::string_view opfXml,
                                               std::string_view opfBaseDir,
                                               const EpubConverter::Options& options) {
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

    std::vector<TocEntry> readToc(EpubZip::Archive& zip, const std::vector<ManifestItem>& manifest,
                                  std::string_view opfXml,
                                  std::string_view bookTitle) {
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

    WritingMode explicitWritingMode(EpubZip::Archive& zip, const std::vector<ManifestItem>& manifest) {
        for (const ManifestItem& item: manifest) {
            if (toLowerCopy(item.mediaType) != "text/css")
                continue;
            std::string css;
            if (!zip.extractToString(item.path, css, kMaxCssBytes))
                continue;
            std::ranges::transform(css, css.begin(), AsciiText::toLower);
            css.erase(std::remove_if(css.begin(), css.end(), AsciiText::isWhitespace), css.end());
            if (css.contains("writing-mode:vertical-rl") || css.contains("-epub-writing-mode:vertical-rl"))
                return WritingMode::verticalRl;
        }
        return WritingMode::horizontalTb;
    }

    RsvpWriter createWriter(File& output, std::string_view epubPath, std::string_view opfXml, WritingMode writingMode,
                            size_t maxWords) {
        const std::string title = [&]() {
            const std::string metadataTitle = parseDcMetadata(opfXml, "title");
            return metadataTitle.empty() ? basenameWithoutExtension(epubPath) : metadataTitle;
        }();
        const std::string author = parseDcMetadata(opfXml, "creator");
        const auto locale = LocaleTag::normalize(parseDcMetadata(opfXml, "language"));

        return RsvpWriter(output,
                          {.source = epubPath,
                           .title = title,
                           .author = author,
                           .converter = EpubConverter::kVersion,
                           .language = locale ? std::string_view{*locale} : std::string_view{},
                           .verticalWriting = writingMode == WritingMode::verticalRl},
                          maxWords);
    }

    void reportReadingOrderReady(const EpubConverter::Options& options, const std::vector<std::string>& readingOrder) {
        ESP_LOGD("epub", "Reading order contains %u content files", static_cast<unsigned int>(readingOrder.size()));
        const std::string foundDetail = std::to_string(readingOrder.size()) + " content files";
        reportProgress(options, "Opening EPUB", foundDetail.c_str(), 25);
    }

    void streamReadingOrder(EpubZip::Archive& zip, RsvpWriter& writer, const std::vector<std::string>& readingOrder,
                            const std::vector<TocEntry>& tocEntries, std::string_view bookTitle,
                            const EpubConverter::Options& options) {
        const bool hasToc = !tocEntries.empty();

        const auto reportItemProgress = [&](const char* title, size_t itemIndex) {
            const std::string detail = itemProgressDetail(itemIndex, readingOrder.size(), writer.wordCount());
            reportProgress(options, title, detail.c_str(), contentProgressPercent(itemIndex, readingOrder.size()));
        };

        for (size_t i = 0; i < readingOrder.size() && !writer.reachedWordLimit(); ++i) {
            serviceBackground();

            reportItemProgress("Extracting content", i);

            std::vector<TocEntry> documentTocEntries;
            std::copy_if(tocEntries.begin(), tocEntries.end(), std::back_inserter(documentTocEntries),
                         [&](const TocEntry& entry) {
                             return std::ranges::equal(entry.path, readingOrder[i], {}, AsciiText::toLower,
                                                       AsciiText::toLower);
                         });

            const EpubZip::ContentExtractStatus extractStatus =
                zip.extractContentToRsvp(readingOrder[i], writer, documentTocEntries, hasToc,
                                         fallbackChapterTitle(readingOrder[i]), bookTitle, options, i,
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

    bool convertEpubToRsvp(std::string_view epubPath, std::string_view rsvpPath,
                           const EpubConverter::Options& options) {
        const std::string sourcePath{epubPath};
        const std::string outputPath{rsvpPath};
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

        const std::vector<ManifestItem> manifest = parseManifestItems(documents.opfXml, documents.opfBaseDir);
        const std::vector<std::string> readingOrder =
            buildReadingOrder(manifest, documents.opfXml, documents.opfBaseDir, options);
        if (readingOrder.empty()) {
            ESP_LOGD("epub", "No readable XHTML spine items found");
            return failWithClosedZip();
        }
        reportReadingOrderReady(options, readingOrder);

        const std::string bookTitle = [&]() {
            const std::string metadataTitle = parseDcMetadata(documents.opfXml, "title");
            return metadataTitle.empty() ? basenameWithoutExtension(epubPath) : metadataTitle;
        }();
        const std::vector<TocEntry> tocEntries = readToc(zip, manifest, documents.opfXml, bookTitle);
        const WritingMode writingMode = explicitWritingMode(zip, manifest);
        ESP_LOGD("epub", "Usable TOC entries: %u", static_cast<unsigned int>(tocEntries.size()));

        Board::Storage::filesystem().remove(outputPath.c_str());
        File output = Board::Storage::filesystem().open(outputPath.c_str(), FILE_WRITE);
        if (!output) {
            ESP_LOGE("epub", "Could not create RSVP file: %s", outputPath.c_str());
            return failWithClosedZip();
        }

        RsvpWriter writer = createWriter(output, epubPath, documents.opfXml, writingMode, options.conversion.maxWords);
        streamReadingOrder(zip, writer, readingOrder, tocEntries, bookTitle, options);

        const std::string finishingDetail = wordCountDetail(writer.wordCount());
        reportProgress(options, "Finishing EPUB", finishingDetail.c_str(), 96);
        const bool flushed = writer.finish();
        output.close();
        zip.close();

        if (!flushed || writer.wordCount() == 0) {
            ESP_LOGD("epub", "No readable words extracted from %s", sourcePath.c_str());
            Board::Storage::filesystem().remove(outputPath.c_str());
            return false;
        }

        ESP_LOGI("epub", "Converted %.*s -> %.*s (%u words)", static_cast<int>(epubPath.size()), epubPath.data(),
                 static_cast<int>(rsvpPath.size()), rsvpPath.data(), static_cast<unsigned int>(writer.wordCount()));
        const std::string convertedDetail = wordCountDetail(writer.wordCount());
        reportProgress(options, "EPUB converted", convertedDetail.c_str(), 100);
        return true;
    }

} // namespace

std::expected<void, std::error_code> EpubConverter::convert(std::string_view epubPath, std::string_view rsvpPath,
                                                            const Options& options) {
    return convertEpubToRsvp(epubPath, rsvpPath, options)
             ? std::expected<void, std::error_code>{}
             : std::unexpected(std::make_error_code(std::errc::io_error));
}
