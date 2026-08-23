#include "storage/index/IndexedBook.h"
#include <esp_log.h>
#include "logging/Logger.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <esp_heap_caps.h>
#include <limits>
#include <system_error>
#include "board/BoardStorage.h"

#include "storage/fs/StorageFiles.h"
#include "storage/fs/StoragePaths.h"
#include "storage/index/BufferedWriter.h"
#include "storage/library/EpubCache.h"
#include "text/RsvpDirectives.h"
#include "text/RsvpTokenizer.h"

#ifndef RSVP_ON_DEVICE_EPUB_CONVERSION
#define RSVP_ON_DEVICE_EPUB_CONVERSION 0
#endif

namespace IndexedBook {

    using IndexHeader = IndexedBookStore::Header;
    using WordRecord = IndexedBookStore::WordRecord;
    using ChapterRecord = IndexedBookStore::ChapterRecord;

    namespace {

        using namespace StoragePaths;

        constexpr uint32_t kFnv1aOffsetBasis = 2166136261UL;
        constexpr uint32_t kFnv1aPrime = 16777619UL;
        constexpr size_t kFingerprintSampleBytes = 512;
        constexpr size_t kParseBufferBytes = 4096;
        constexpr size_t kSidecarWriteBufferBytes = 16 * 1024;
        constexpr size_t kIndexProgressStepBytes = 256UL * 1024UL;
        constexpr size_t kParseMemoryCheckWordInterval = 512;
        constexpr size_t kParseMinFreeHeapBytes = 32 * 1024;
        constexpr size_t kParseMinLargestHeapBlockBytes = 8 * 1024;

        struct IndexedBuildContext {
            BufferedWriter* indexWriter = nullptr;
            BufferedWriter* dataWriter = nullptr;
            BookMetadata* metadata = nullptr;
            uint32_t wordCount = 0;
            uint32_t dataSize = 0;
            bool failed = false;
            const char* failure = "";
        };

        bool readExact(File& file, void* data, size_t bytes) {
            return file.read(reinterpret_cast<uint8_t*>(data), bytes) == bytes;
        }

        bool checkedAdd(uint32_t left, uint32_t right, uint32_t& result) {
            if (left > std::numeric_limits<uint32_t>::max() - right) {
                return false;
            }
            result = left + right;
            return true;
        }

        bool indexHeaderLayoutValid(const IndexHeader& header, size_t indexBytes, size_t dataBytes) {
            uint32_t recordsBytes = 0;
            uint32_t recordsEnd = 0;
            uint32_t paragraphsEnd = 0;
            uint32_t chaptersEnd = 0;
            if (header.wordCount > std::numeric_limits<uint32_t>::max() / sizeof(WordRecord)
                || header.paragraphCount > std::numeric_limits<uint32_t>::max() / sizeof(uint32_t)
                || header.chapterCount > std::numeric_limits<uint32_t>::max() / sizeof(ChapterRecord)) {
                return false;
            }

            recordsBytes = header.wordCount * sizeof(WordRecord);
            return checkedAdd(header.recordsOffset, recordsBytes, recordsEnd)
                && checkedAdd(header.paragraphsOffset, header.paragraphCount * sizeof(uint32_t), paragraphsEnd)
                && checkedAdd(header.chaptersOffset, header.chapterCount * sizeof(ChapterRecord), chaptersEnd)
                && header.recordsOffset >= sizeof(IndexHeader) && header.paragraphsOffset == recordsEnd
                && header.chaptersOffset == paragraphsEnd && chaptersEnd <= indexBytes && header.dataSize <= dataBytes;
        }

        uint32_t fnv1aUpdate(uint32_t hash, const uint8_t* data, size_t bytes) {
            for (size_t i = 0; i < bytes; ++i) {
                hash ^= data[i];
                hash *= kFnv1aPrime;
            }
            return hash;
        }

        uint32_t sourceFingerprint(File& file, uint32_t sourceSize) {
            uint32_t hash = kFnv1aOffsetBasis;
            const std::array<uint8_t, 4> sizeBytes = {{
                static_cast<uint8_t>(sourceSize & 0xFF),
                static_cast<uint8_t>((sourceSize >> 8) & 0xFF),
                static_cast<uint8_t>((sourceSize >> 16) & 0xFF),
                static_cast<uint8_t>((sourceSize >> 24) & 0xFF),
            }};
            hash = fnv1aUpdate(hash, sizeBytes.data(), sizeBytes.size());

            uint8_t buffer[kFingerprintSampleBytes];
            const bool hasFullSample = sourceSize > kFingerprintSampleBytes;
            const uint32_t sampleBytes = static_cast<uint32_t>(kFingerprintSampleBytes);

            const std::array<uint32_t, 3> offsets = {
                0,
                hasFullSample ? sourceSize / 2 : 0,
                hasFullSample ? sourceSize - sampleBytes : 0,
            };

            for (uint32_t offset: offsets) {
                if (!file.seek(offset)) {
                    continue;
                }
                const size_t wanted =
                    static_cast<size_t>(std::min<uint32_t>(kFingerprintSampleBytes, sourceSize - offset));
                const size_t read = file.read(buffer, wanted);
                hash = fnv1aUpdate(hash, buffer, read);
            }

            return hash;
        }

        bool readIndexHeader(std::string_view path, IndexHeader& header) {
            const std::string indexPath = indexedIndexPathFor(path);
            File file = Board::Storage::filesystem().open(indexPath.c_str(), FILE_READ);
            if (!file) {
                return false;
            }

            if (file.isDirectory()) {
                file.close();
                return false;
            }

            const bool ok = readExact(file, &header, sizeof(header));
            file.close();

            return ok && header.magic == IndexedBookStore::kMagic && header.version == IndexedBookStore::kVersion
                && header.headerSize == sizeof(IndexHeader) && header.recordSize == sizeof(WordRecord)
                && header.recordsOffset >= sizeof(IndexHeader);
        }

        bool parseMemoryLow() {
            return heap_caps_get_free_size(MALLOC_CAP_8BIT) < kParseMinFreeHeapBytes
                || heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < kParseMinLargestHeapBlockBytes;
        }

        void addChapterMarker(IndexedBuildContext& buildContext, std::string_view title) {
            if (title.empty() || buildContext.metadata == nullptr) {
                return;
            }

            ChapterMarker marker;
            marker.title = title;
            marker.wordIndex = buildContext.wordCount;

            if (!buildContext.metadata->chapters.empty()
                && buildContext.metadata->chapters.back().wordIndex == marker.wordIndex) {
                buildContext.metadata->chapters.back() = marker;
                return;
            }

            buildContext.metadata->chapters.push_back(marker);
        }

        void addParagraphMarker(IndexedBuildContext& buildContext) {
            if (buildContext.metadata == nullptr) {
                return;
            }

            const size_t wordIndex = buildContext.wordCount;
            if (!buildContext.metadata->paragraphStarts.empty()
                && buildContext.metadata->paragraphStarts.back() == wordIndex) {
                return;
            }

            buildContext.metadata->paragraphStarts.push_back(wordIndex);
        }

        bool pushWord(std::string token, IndexedBuildContext& buildContext, RsvpText::ParseStats* stats) {
            if (token.empty() || (!RsvpText::hasReadableText(token) && token != "-")) {
                return true;
            }

            if (token.length() > UINT16_MAX
                || buildContext.dataSize > UINT32_MAX - static_cast<uint32_t>(token.length())) {
                buildContext.failed = true;
                buildContext.failure = "Index limit reached";
                return false;
            }

            if ((buildContext.wordCount % kParseMemoryCheckWordInterval) == 0 && buildContext.wordCount > 0
                && parseMemoryLow()) {
                if (stats != nullptr) {
                    stats->memoryLow = true;
                }
                buildContext.failed = true;
                buildContext.failure = "Memory limit reached";
                return false;
            }

            WordRecord record;
            record.offset = buildContext.dataSize;
            record.length = static_cast<uint16_t>(token.length());

            if (buildContext.dataWriter == nullptr && buildContext.indexWriter == nullptr) {
                buildContext.failed = true;
                buildContext.failure = "Index writer missing";
                return false;
            }

            if (buildContext.dataWriter != nullptr && !buildContext.dataWriter->write(token.data(), token.length())) {
                buildContext.failed = true;
                buildContext.failure = "SD write failed";
                return false;
            }

            if (buildContext.indexWriter != nullptr && !buildContext.indexWriter->write(&record, sizeof(record))) {
                buildContext.failed = true;
                buildContext.failure = "SD write failed";
                return false;
            }

            buildContext.dataSize += static_cast<uint32_t>(token.length());
            ++buildContext.wordCount;
            if (buildContext.metadata != nullptr) {
                buildContext.metadata->wordCount = buildContext.wordCount;
            }
            return true;
        }

        bool appendLineWords(std::string_view line, IndexedBuildContext& buildContext, RsvpText::ParseStats* stats) {
            return RsvpText::appendLineWords(
                line,
                [&](const std::string& token) {
                    return pushWord(token, buildContext, stats);
                },
                buildContext.wordCount, stats);
        }

        bool processBookLine(std::string_view line, IndexedBuildContext& buildContext, bool& paragraphPending,
                             RsvpText::ParseStats* stats) {
            const std::string_view trimmed = RsvpText::stripBom(line);
            if (trimmed.empty()) {
                paragraphPending = true;
                return true;
            }

            std::string chapterTitle;
            if (RsvpText::chapterTitleFromLine(line, chapterTitle)) {
                addChapterMarker(buildContext, chapterTitle);
                paragraphPending = true;
            }

            if (paragraphPending) {
                addParagraphMarker(buildContext);
                paragraphPending = false;
            }
            return appendLineWords(line, buildContext, stats);
        }

        bool processRsvpLine(std::string_view line, IndexedBuildContext& buildContext, bool& paragraphPending,
                             RsvpText::ParseStats* stats) {
            std::string trimmed{RsvpText::stripBom(line)};
            if (trimmed.empty()) {
                paragraphPending = true;
                return true;
            }

            if (trimmed.starts_with("@@")) {
                trimmed.erase(0, 1);
                if (paragraphPending) {
                    addParagraphMarker(buildContext);
                    paragraphPending = false;
                }
                return appendLineWords(trimmed, buildContext, stats);
            }

            if (trimmed.starts_with('@')) {
                if (RsvpText::prefixHasBoundary(trimmed, "@para")) {
                    paragraphPending = true;
                    return true;
                }
                if (RsvpText::prefixHasBoundary(trimmed, "@chapter")) {
                    std::string title = RsvpText::directiveValue(trimmed, "@chapter");
                    if (title.empty()) {
                        title = "Chapter";
                    }
                    addChapterMarker(buildContext, title);
                    paragraphPending = true;
                    return true;
                }
                if (buildContext.metadata != nullptr && RsvpText::prefixHasBoundary(trimmed, "@title")) {
                    buildContext.metadata->title = RsvpText::directiveValue(trimmed, "@title");
                    return true;
                }
                if (buildContext.metadata != nullptr && RsvpText::prefixHasBoundary(trimmed, "@author")) {
                    buildContext.metadata->author = RsvpText::directiveValue(trimmed, "@author");
                    return true;
                }
                return true;
            }

            if (paragraphPending) {
                addParagraphMarker(buildContext);
                paragraphPending = false;
            }
            return appendLineWords(line, buildContext, stats);
        }

        bool readIndexedMetadata(std::string_view path, BookMetadata& metadata, IndexHeader* headerOut = nullptr) {
            metadata.clear();
            const std::string sourcePath{path};
            const std::string indexPath = indexedIndexPathFor(path);
            const std::string dataPath = indexedDataPathFor(path);

            IndexHeader header;
            if (!readIndexHeader(path, header)) {
                if (StorageFiles::fileExistsWithBytes(indexPath.c_str())) {
                    ESP_LOGW("storage-index", "invalid index header: %s", indexPath.c_str());
                }
                return false;
            }

            {
                // Validate that the sidecar still matches the source file.
                File source = Board::Storage::filesystem().open(sourcePath.c_str(), FILE_READ);
                if (!source || source.isDirectory()) {
                    if (source) {
                        source.close();
                    }
                    ESP_LOGW("storage-index", "source missing while validating index: %s", sourcePath.c_str());
                    return false;
                }

                const size_t sourceBytes = source.size();
                const uint32_t actualFingerprint =
                    sourceBytes <= UINT32_MAX ? sourceFingerprint(source, static_cast<uint32_t>(sourceBytes)) : 0;
                source.close();
                if (sourceBytes > UINT32_MAX || header.sourceSize != static_cast<uint32_t>(sourceBytes)
                    || header.sourceFingerprint != actualFingerprint) {
                    ESP_LOGW("storage-index", "stale index: %s size=%lu/%lu fingerprint=%08lx/%08lx",
                             sourcePath.c_str(), static_cast<unsigned long>(header.sourceSize),
                             static_cast<unsigned long>(sourceBytes),
                             static_cast<unsigned long>(header.sourceFingerprint),
                             static_cast<unsigned long>(actualFingerprint));
                    return false;
                }
            }

            {
                // Ensure the word data sidecar is present and large enough.
                File data = Board::Storage::filesystem().open(dataPath.c_str(), FILE_READ);
                if (!data || data.isDirectory() || data.size() < header.dataSize) {
                    const size_t dataBytes = data ? data.size() : 0;
                    if (data) {
                        data.close();
                    }
                    ESP_LOGW("storage-index", "data sidecar invalid: %s size=%lu expected=%lu", dataPath.c_str(),
                             static_cast<unsigned long>(dataBytes), static_cast<unsigned long>(header.dataSize));
                    return false;
                }
                data.close();
            }

            File indexFile = Board::Storage::filesystem().open(indexPath.c_str(), FILE_READ);
            if (!indexFile || indexFile.isDirectory()) {
                if (indexFile) {
                    indexFile.close();
                }
                ESP_LOGE("storage-index", "index sidecar cannot reopen: %s", indexPath.c_str());
                return false;
            }

            // Validate index table offsets before loading paragraph/chapter metadata.
            File dataFile = Board::Storage::filesystem().open(dataPath.c_str(), FILE_READ);
            const size_t dataBytes = dataFile ? dataFile.size() : 0;
            if (dataFile) {
                dataFile.close();
            }
            if (!indexHeaderLayoutValid(header, indexFile.size(), dataBytes)) {
                indexFile.close();
                metadata.clear();
                ESP_LOGW("storage-index", "index layout invalid: %s", indexPath.c_str());
                return false;
            }

            metadata.wordCount = header.wordCount;
            const RsvpText::RsvpDirectiveValues directives = RsvpText::readRsvpDirectiveValues(sourcePath);
            metadata.title = directives.title;
            metadata.author = directives.author;
            if (metadata.title.empty()) {
                metadata.title = RsvpText::normalizeDisplayText(displayNameWithoutExtension(path));
            }

            if (header.paragraphCount > 0) {
                metadata.paragraphStarts.reserve(header.paragraphCount);
                if (!indexFile.seek(header.paragraphsOffset)) {
                    indexFile.close();
                    metadata.clear();
                    ESP_LOGE("storage-index", "paragraph section seek failed: %s offset=%lu", indexPath.c_str(),
                             static_cast<unsigned long>(header.paragraphsOffset));
                    return false;
                }
                for (uint32_t i = 0; i < header.paragraphCount; ++i) {
                    uint32_t wordIndex = 0;
                    if (!readExact(indexFile, &wordIndex, sizeof(wordIndex))) {
                        indexFile.close();
                        metadata.clear();
                        ESP_LOGE("storage-index", "paragraph section read failed: %s item=%lu", indexPath.c_str(),
                                 static_cast<unsigned long>(i));
                        return false;
                    }
                    metadata.paragraphStarts.push_back(wordIndex);
                }
            }

            if (header.chapterCount > 0) {
                metadata.chapters.reserve(header.chapterCount);
                if (!indexFile.seek(header.chaptersOffset)) {
                    indexFile.close();
                    metadata.clear();
                    ESP_LOGE("storage-index", "chapter section seek failed: %s offset=%lu", indexPath.c_str(),
                             static_cast<unsigned long>(header.chaptersOffset));
                    return false;
                }
                for (uint32_t i = 0; i < header.chapterCount; ++i) {
                    ChapterRecord record;
                    if (!readExact(indexFile, &record, sizeof(record))) {
                        indexFile.close();
                        metadata.clear();
                        ESP_LOGE("storage-index", "chapter section read failed: %s item=%lu", indexPath.c_str(),
                                 static_cast<unsigned long>(i));
                        return false;
                    }
                    ChapterMarker marker;
                    marker.wordIndex = record.wordIndex;
                    const uint32_t titleLength = std::min<uint32_t>(record.titleLength, sizeof(record.title));
                    marker.title.assign(record.title, titleLength);
                    metadata.chapters.push_back(marker);
                }
            }

            indexFile.close();
            if (metadata.wordCount > 0 && metadata.paragraphStarts.empty()) {
                metadata.paragraphStarts.push_back(0);
            }
            if (headerOut != nullptr) {
                *headerOut = header;
            }
            if (metadata.wordCount == 0) {
                ESP_LOGW("storage-index", "index has no words: %s", indexPath.c_str());
                return false;
            }
            return true;
        }

        bool build(std::string_view path, BookMetadata& metadata, bool rsvpFormat, StatusCallback statusCallback,
                   void* statusContext) {
            metadata.clear();
            const std::string sourcePath{path};
            const std::string label = displayNameForPath(path);
            const std::string indexPath = indexedIndexPathFor(path);
            const std::string dataPath = indexedDataPathFor(path);
            const std::string tmpIndexPath = indexedTempPathFor(indexPath);
            const std::string tmpDataPath = indexedTempPathFor(dataPath);
            const std::string sidecarDirectory = parentDirectoryForPath(path);
            auto report = [&](const char* title, const char* line1 = "", const char* line2 = "",
                              int progressPercent = -1) {
                if (statusCallback != nullptr) {
                    statusCallback(statusContext, title, line1, line2, progressPercent);
                }
            };

            File source = Board::Storage::filesystem().open(sourcePath.c_str(), FILE_READ);
            if (!source || source.isDirectory()) {
                if (source) {
                    source.close();
                }
                ESP_LOGE("storage-index", "cannot open source: %s", sourcePath.c_str());
                report("Index failed", displayNameForPath(path).c_str(), "File unreadable", 100);
                return false;
            }

            const size_t sourceBytes = source.size();
            if (sourceBytes == 0 || sourceBytes > UINT32_MAX) {
                source.close();
                ESP_LOGW("storage-index", "unsupported source size: %s (%lu bytes)", sourcePath.c_str(),
                         static_cast<unsigned long>(sourceBytes));
                report("Index failed", displayNameForPath(path).c_str(),
                       sourceBytes == 0 ? "No readable words" : "Book too large", 100);
                return false;
            }
            const uint32_t fingerprint = sourceFingerprint(source, static_cast<uint32_t>(sourceBytes));
            if (!source.seek(0)) {
                source.close();
                ESP_LOGE("storage-index", "source rewind failed: %s", sourcePath.c_str());
                report("Index failed", displayNameForPath(path).c_str(), "Source read failed", 100);
                return false;
            }

            report("Indexing book", label.c_str(), "Building word index", 0);

            if (!StorageFiles::directoryExists(sidecarDirectory.c_str())) {
                source.close();
                ESP_LOGW("storage-index", "sidecar parent missing/not directory: %s", sidecarDirectory.c_str());
                report("Index failed", label.c_str(), "Folder missing", 100);
                return false;
            }

            Board::Storage::filesystem().remove(tmpIndexPath.c_str());
            Board::Storage::filesystem().remove(tmpDataPath.c_str());
            auto removeTempSidecars = [&]() {
                Board::Storage::filesystem().remove(tmpIndexPath.c_str());
                Board::Storage::filesystem().remove(tmpDataPath.c_str());
            };

            // One streaming parser feeds either data sidecar writes or index record
            // writes.
            auto parseIndexedSource = [&](File& parseSource, IndexedBuildContext& buildContext,
                                          RsvpText::ParseStats& stats, bool reportProgress) -> bool {
                std::string line;
                line.reserve(256);
                bool paragraphPending = true;
                bool keepReading = true;
                bool parseFailed = false;

                static uint8_t buf[kParseBufferBytes];
                size_t totalBytesRead = 0;
                size_t nextProgressBytes = 0;

                auto processLine = [&](std::string_view lineToProcess) {
                    return rsvpFormat ? processRsvpLine(lineToProcess, buildContext, paragraphPending, &stats)
                                      : processBookLine(lineToProcess, buildContext, paragraphPending, &stats);
                };

                while (keepReading && parseSource.available()) {
                    const size_t bytesRead = parseSource.read(buf, kParseBufferBytes);
                    if (bytesRead == 0) {
                        break;
                    }
                    totalBytesRead += bytesRead;

                    if (reportProgress && sourceBytes > 0 && totalBytesRead >= nextProgressBytes) {
                        const int progress =
                            static_cast<int>(std::min<size_t>(90, (totalBytesRead * 90UL) / sourceBytes));
                        report("Indexing book", label.c_str(), "Building word index", progress);
                        nextProgressBytes = totalBytesRead + kIndexProgressStepBytes;
                    }
                    yield();

                    for (size_t i = 0; i < bytesRead && keepReading; ++i) {
                        const char c = static_cast<char>(buf[i]);

                        if (c == '\r') {
                            continue;
                        }

                        if (c == '\n') {
                            keepReading = processLine(line);
                            if (!keepReading && RsvpText::kMaxBookWords > 0) {
                                ESP_LOGD("storage-index", "Reached %lu word limit, truncating book",
                                         static_cast<unsigned long>(RsvpText::kMaxBookWords));
                            } else if (!keepReading && (stats.memoryLow || buildContext.failed)) {
                                parseFailed = true;
                            }
                            line.clear();
                            continue;
                        }

                        line += c;
                        if (line.length() >= RsvpText::kMaxBookLineChars) {
                            keepReading = processLine(line);
                            ++stats.longLineSplits;
                            if (!keepReading && (stats.memoryLow || buildContext.failed)) {
                                parseFailed = true;
                            }
                            line.clear();
                        }
                    }
                }

                if (!line.empty() && keepReading
                    && (RsvpText::kMaxBookWords == 0 || buildContext.wordCount < RsvpText::kMaxBookWords)) {
                    keepReading = processLine(line);
                    if (!keepReading && (stats.memoryLow || buildContext.failed)) {
                        parseFailed = true;
                    }
                }

                return !parseFailed;
            };

            IndexedBuildContext dataContext;
            RsvpText::ParseStats stats;
            const uint32_t startedMs = millis();
            IndexHeader header;
            bool parseFailed = false;

            {
                // First pass writes word data and records metadata.
                errno = 0;
                File dataFile = Board::Storage::filesystem().open(tmpDataPath.c_str(), FILE_WRITE);
                const int dataOpenErrno = errno;
                if (!dataFile) {
                    Logger::failure("storage-index", "open data FILE_WRITE", tmpDataPath.c_str(),
                                    std::error_code{dataOpenErrno, std::generic_category()});
                    source.close();
                    removeTempSidecars();
                    report("Index failed", label.c_str(), "SD write failed", 100);
                    return false;
                }

                BufferedWriter dataWriter(dataFile, kSidecarWriteBufferBytes);
                dataContext.dataWriter = &dataWriter;
                dataContext.metadata = &metadata;

                parseFailed = !parseIndexedSource(source, dataContext, stats, true);

                if (stats.longLineSplits > 0 || stats.normalization.malformedUtf8 > 0
                    || stats.normalization.nonAsciiCodepoints > 0) {
                    ESP_LOGD("storage-index", "Parse cleanup: long_lines=%u malformed_utf8=%u non_ascii=%u",
                             static_cast<unsigned int>(stats.longLineSplits),
                             static_cast<unsigned int>(stats.normalization.malformedUtf8),
                             static_cast<unsigned int>(stats.normalization.nonAsciiCodepoints));
                }

                if (parseFailed || dataContext.wordCount == 0) {
                    const char* detail = dataContext.failure[0] == '\0' ? "No readable words" : dataContext.failure;
                    dataWriter.discard();
                    dataFile.close();
                    source.close();
                    removeTempSidecars();
                    metadata.clear();
                    report("Index failed", label.c_str(), detail, 100);
                    return false;
                }

                if (metadata.paragraphStarts.empty()) {
                    metadata.paragraphStarts.push_back(0);
                }
                if (metadata.title.empty()) {
                    metadata.title = RsvpText::normalizeDisplayText(displayNameWithoutExtension(path));
                }

                header.magic = IndexedBookStore::kMagic;
                header.version = IndexedBookStore::kVersion;
                header.headerSize = sizeof(IndexHeader);
                header.recordSize = sizeof(WordRecord);
                header.sourceSize = static_cast<uint32_t>(sourceBytes);
                header.sourceFingerprint = fingerprint;
                header.wordCount = dataContext.wordCount;
                header.paragraphCount = static_cast<uint32_t>(metadata.paragraphStarts.size());
                header.chapterCount = static_cast<uint32_t>(metadata.chapters.size());
                header.recordsOffset = sizeof(IndexHeader);
                header.paragraphsOffset = header.recordsOffset + header.wordCount * sizeof(WordRecord);
                header.chaptersOffset = header.paragraphsOffset + header.paragraphCount * sizeof(uint32_t);
                header.dataSize = dataContext.dataSize;

                if (!dataWriter.flush()) {
                    ESP_LOGE("storage-index", "data sidecar flush failed: %s", tmpDataPath.c_str());
                    parseFailed = true;
                }
                dataWriter.discard();
                dataFile.close();
                source.close();
            }

            if (parseFailed) {
                removeTempSidecars();
                metadata.clear();
                report("Index failed", label.c_str(), "SD write failed", 100);
                return false;
            }

            {
                // Second pass writes index records and metadata tables.
                source = Board::Storage::filesystem().open(sourcePath.c_str(), FILE_READ);
                if (!source || source.isDirectory()) {
                    if (source) {
                        source.close();
                    }
                    removeTempSidecars();
                    metadata.clear();
                    ESP_LOGE("storage-index", "cannot reopen source for index: %s", sourcePath.c_str());
                    report("Index failed", label.c_str(), "Source read failed", 100);
                    return false;
                }

                errno = 0;
                File indexFile = Board::Storage::filesystem().open(tmpIndexPath.c_str(), FILE_WRITE);
                const int indexOpenErrno = errno;
                if (!indexFile) {
                    Logger::failure("storage-index", "open index FILE_WRITE", tmpIndexPath.c_str(),
                                    std::error_code{indexOpenErrno, std::generic_category()});
                    source.close();
                    removeTempSidecars();
                    report("Index failed", label.c_str(), "SD write failed", 100);
                    return false;
                }

                BufferedWriter indexWriter(indexFile, kSidecarWriteBufferBytes);
                if (!indexWriter.write(&header, sizeof(header))) {
                    indexWriter.discard();
                    indexFile.close();
                    source.close();
                    removeTempSidecars();
                    report("Index failed", label.c_str(), "SD write failed", 100);
                    return false;
                }

                IndexedBuildContext indexContext;
                indexContext.indexWriter = &indexWriter;
                RsvpText::ParseStats indexStats;
                parseFailed = !parseIndexedSource(source, indexContext, indexStats, false);
                if (parseFailed) {
                    ESP_LOGE("storage-index", "second pass parse/write failed: %s detail=%s", sourcePath.c_str(),
                             indexContext.failure);
                }

                if (!parseFailed
                    && (indexContext.wordCount != header.wordCount || indexContext.dataSize != header.dataSize)) {
                    ESP_LOGE("storage-index", "second pass mismatch words=%u/%u data=%u/%u",
                             static_cast<unsigned int>(indexContext.wordCount),
                             static_cast<unsigned int>(header.wordCount),
                             static_cast<unsigned int>(indexContext.dataSize),
                             static_cast<unsigned int>(header.dataSize));
                    parseFailed = true;
                }

                // Word records end exactly where the paragraph table begins, so keep
                // the writer streaming sequentially.
                for (size_t i = 0; !parseFailed && i < metadata.paragraphStarts.size(); ++i) {
                    const uint32_t wordIndex = static_cast<uint32_t>(metadata.paragraphStarts[i]);
                    if (!indexWriter.write(&wordIndex, sizeof(wordIndex))) {
                        ESP_LOGE("storage-index", "paragraph table write failed: %s item=%u", tmpIndexPath.c_str(),
                                 static_cast<unsigned int>(i));
                        parseFailed = true;
                    }
                }

                // Chapter markers follow the paragraph table without a gap.
                for (size_t i = 0; !parseFailed && i < metadata.chapters.size(); ++i) {
                    ChapterRecord record;
                    record.wordIndex = static_cast<uint32_t>(metadata.chapters[i].wordIndex);
                    const std::string& title = metadata.chapters[i].title;
                    record.titleLength = std::min<uint32_t>(title.size(), sizeof(record.title));
                    for (uint32_t j = 0; j < record.titleLength; ++j) {
                        record.title[j] = title[j];
                    }
                    if (!indexWriter.write(&record, sizeof(record))) {
                        ESP_LOGE("storage-index", "chapter table write failed: %s item=%u", tmpIndexPath.c_str(),
                                 static_cast<unsigned int>(i));
                        parseFailed = true;
                    }
                }

                // Rewrite the header last so partially written indexes are rejected later.
                if (!parseFailed && !indexWriter.seek(0)) {
                    ESP_LOGE("storage-index", "final header seek failed: %s", tmpIndexPath.c_str());
                    parseFailed = true;
                }
                if (!parseFailed && !indexWriter.write(&header, sizeof(header))) {
                    ESP_LOGE("storage-index", "final header write failed: %s", tmpIndexPath.c_str());
                    parseFailed = true;
                }
                if (!parseFailed && !indexWriter.flush()) {
                    ESP_LOGE("storage-index", "index sidecar flush failed: %s", tmpIndexPath.c_str());
                    parseFailed = true;
                }

                indexWriter.discard();
                indexFile.close();
                source.close();
            }

            if (parseFailed) {
                removeTempSidecars();
                metadata.clear();
                report("Index failed", label.c_str(), "SD write failed", 100);
                return false;
            }

            // Commit both sidecars only after the full two-pass build succeeds.
            Board::Storage::filesystem().remove(indexPath.c_str());
            Board::Storage::filesystem().remove(dataPath.c_str());
            errno = 0;
            const bool dataRenamed = Board::Storage::filesystem().rename(tmpDataPath.c_str(), dataPath.c_str());
            const int dataRenameErrno = errno;
            bool indexRenamed = false;
            int indexRenameErrno = 0;
            if (dataRenamed) {
                errno = 0;
                indexRenamed = Board::Storage::filesystem().rename(tmpIndexPath.c_str(), indexPath.c_str());
                indexRenameErrno = errno;
            }
            const bool renamed = indexRenamed && dataRenamed;
            if (!renamed) {
                if (!dataRenamed) {
                    Logger::failure("storage-index", "rename data", tmpDataPath.c_str(), dataPath.c_str(),
                                    std::error_code{dataRenameErrno, std::generic_category()});
                }
                if (!indexRenamed) {
                    Logger::failure("storage-index", "rename index", tmpIndexPath.c_str(), indexPath.c_str(),
                                    std::error_code{indexRenameErrno, std::generic_category()});
                }
                Board::Storage::filesystem().remove(tmpIndexPath.c_str());
                Board::Storage::filesystem().remove(tmpDataPath.c_str());
                Board::Storage::filesystem().remove(indexPath.c_str());
                Board::Storage::filesystem().remove(dataPath.c_str());
                metadata.clear();
                report("Index failed", label.c_str(), "Rename failed", 100);
                return false;
            }

            ESP_LOGI("storage-index", "Built %u words, %u chapters from %s in %lu ms",
                     static_cast<unsigned int>(metadata.wordCount), static_cast<unsigned int>(metadata.chapters.size()),
                     sourcePath.c_str(), static_cast<unsigned long>(millis() - startedMs));
            report("Index ready", label.c_str(), "Book ready", 100);
            return true;
        }

    } // namespace

    bool load(size_t index, BookLibrary::Listing& library, IndexedBookStore& store, BookMetadata& metadata,
              const OpenRequest& request) {
        metadata.clear();
        auto report = [&](const char* title, const char* line1 = "", const char* line2 = "", int progressPercent = -1) {
            if (request.statusCallback != nullptr) {
                request.statusCallback(request.statusContext, title, line1, line2, progressPercent);
            }
        };

        std::string path;
        size_t parsedIndex = index;

        {
            // Library selection and EPUB preparation.
            if (!StorageFiles::directoryExists(kBooksPath)) {
                ESP_LOGE("storage", "/books directory not found");
                report("Book open failed", "Folders missing", "Run SD check", 100);
                return false;
            }

            if (library.paths.empty()) {
                BookLibrary::refresh(library, false, RSVP_ON_DEVICE_EPUB_CONVERSION);
            }
            if (library.paths.empty()) {
                ESP_LOGD("storage", "No readable .rsvp, .txt, or .epub books found under /books");
                report("Book open failed", "No books found", "Add books to SD", 100);
                return false;
            }

            if (index >= library.paths.size()) {
                ESP_LOGW("storage", "Book index %u out of range", static_cast<unsigned int>(index));
                report("Book open failed", "Library changed", "Open list again", 100);
                return false;
            }

            path = BookLibrary::pathAt(library, index);
            if (hasEpubExtension(path)) {
                if (!request.allowEpubConversion) {
                    report("Index needed", displayNameForPath(path).c_str(), "Open from library", 100);
                    return false;
                }

                auto rsvpPath = EpubCache::ensureConverted(path, request.statusCallback, request.statusContext);
                if (!rsvpPath) {
                    return false;
                }

                BookLibrary::refresh(library, true, RSVP_ON_DEVICE_EPUB_CONVERSION);
                const int convertedIndex = BookLibrary::indexOfPath(library, *rsvpPath);
                if (convertedIndex < 0) {
                    ESP_LOGE("storage", "Converted RSVP not found in refreshed library: %s", rsvpPath->c_str());
                    report("Book open failed", displayNameForPath(path).c_str(), "Conversion cache missing", 100);
                    return false;
                }

                path = *rsvpPath;
                parsedIndex = static_cast<size_t>(convertedIndex);
            }
        }

        {
            // Source readability check.
            File entry = Board::Storage::filesystem().open(path.c_str(), FILE_READ);
            if (!entry || entry.isDirectory()) {
                if (entry) {
                    entry.close();
                }
                ESP_LOGE("storage", "selected book is not readable: %s", path.c_str());
                report("Book open failed", displayNameForPath(path).c_str(), "File unreadable", 100);
                return false;
            }
            entry.close();
        }

        {
            // Index validation, optional rebuild, and store open.
            IndexHeader header;
            auto ensureIndexedBook = [&]() -> bool {
                if (readIndexedMetadata(path, metadata, &header)) {
                    report("Opening book", displayNameForPath(path).c_str(), "Index is current", 45);
                    return true;
                }

                if (!request.allowIndexBuild) {
                    report("Index needed", displayNameForPath(path).c_str(), "Open from library", 100);
                    return false;
                }

                ESP_LOGW("storage-index", "rebuilding missing/stale index: %s", path.c_str());
                report("Opening book", displayNameForPath(path).c_str(), "Index needs rebuild", 20);
                if (!build(path, metadata, hasRsvpExtension(path), request.statusCallback, request.statusContext)) {
                    return false;
                }
                if (!readIndexedMetadata(path, metadata, &header)) {
                    ESP_LOGE("storage-index", "freshly built index failed validation: %s", path.c_str());
                    report("Index failed", displayNameForPath(path).c_str(), "Validation failed", 100);
                    return false;
                }
                return true;
            };

            report("Opening book", displayNameForPath(path).c_str(), "Checking index", 12);
            if (!ensureIndexedBook()) {
                metadata.clear();
                return false;
            }

            report("Opening book", displayNameForPath(path).c_str(), "Opening word cache", 80);
            const std::string indexPath = indexedIndexPathFor(path);
            const std::string dataPath = indexedDataPathFor(path);
            if (!store.open(indexPath.c_str(), dataPath.c_str(), header)) {
                metadata.clear();
                report("Book open failed", displayNameForPath(path).c_str(), "Index unreadable", 100);
                return false;
            }
        }

        if (request.loadedPath != nullptr) {
            *request.loadedPath = path;
        }
        if (request.loadedIndex != nullptr) {
            *request.loadedIndex = parsedIndex;
        }

        ESP_LOGI("storage", "Opened indexed book %s: %u words, %u chapters", path.c_str(),
                 static_cast<unsigned int>(metadata.wordCount), static_cast<unsigned int>(metadata.chapters.size()));
        return true;
    }

    bool readMetadata(std::string_view path, BookMetadata& metadata, IndexedBookStore::Header* headerOut) {
        return readIndexedMetadata(path, metadata, headerOut);
    }

} // namespace IndexedBook
