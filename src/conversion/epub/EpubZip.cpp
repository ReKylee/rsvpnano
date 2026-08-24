#include "conversion/epub/EpubZip.h"
#include <esp_log.h>

#include <algorithm>
#include <array>
#include "board/BoardStorage.h"
#include <esp_heap_caps.h>
#include <zlib.h>

#include "conversion/epub/EpubContentParser.h"
#include "conversion/epub/EpubPackage.h"

namespace EpubZip {
    namespace {

        using EpubPackage::isArchiveHintEntry;
        using EpubPackage::normalizeZipName;
        using EpubPackage::toLowerCopy;

        constexpr uint32_t kZipEocdSignature = 0x06054B50UL;
        constexpr uint32_t kZipCentralFileSignature = 0x02014B50UL;
        constexpr uint32_t kZipLocalFileSignature = 0x04034B50UL;
        constexpr uint16_t kZipStored = 0;
        constexpr uint16_t kZipDeflated = 8;
        constexpr size_t kZipEocdMaxSearch = 66UL * 1024UL;
        constexpr uint16_t kMaxZipEntries = 2048;
        constexpr uint16_t kMaxZipNameLength = 512;
        constexpr size_t kReadChunkBytes = 4096;
        constexpr size_t kInflateInputChunkBytes = 4096;

        uint16_t readLe16(const uint8_t* data) {
            return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
        }

        uint32_t readLe32(const uint8_t* data) {
            return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8)
                 | (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
        }

        void serviceBackground() {
            yield();
            delay(0);
        }

        bool readExact(File& file, uint8_t* buffer, size_t length) {
            size_t offset = 0;
            while (offset < length) {
                const size_t chunk = std::min(kReadChunkBytes, length - offset);
                const uint32_t beforePosition = static_cast<uint32_t>(file.position());
                const int bytesRead = file.read(buffer + offset, chunk);
                if (bytesRead != static_cast<int>(chunk)) {
                    ESP_LOGD("epub-zip", "Short read at pos=%lu wanted=%u got=%d totalWanted=%u offset=%u",
                             static_cast<unsigned long>(beforePosition), static_cast<unsigned int>(chunk), bytesRead,
                             static_cast<unsigned int>(length), static_cast<unsigned int>(offset));
                    return false;
                }
                offset += chunk;
                serviceBackground();
            }

            return true;
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

        void* allocateBuffer(size_t bytes) {
            if (bytes == 0) {
                return nullptr;
            }

#if defined(BOARD_HAS_PSRAM)
            return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
            return heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
#endif
        }

        void freeBuffer(void* buffer) {
            if (buffer != nullptr) {
                heap_caps_free(buffer);
            }
        }

        voidpf allocateZlib(voidpf, uInt itemCount, uInt itemSize) {
            if (itemSize != 0 && itemCount > SIZE_MAX / itemSize) {
                return Z_NULL;
            }
            return allocateBuffer(static_cast<size_t>(itemCount) * itemSize);
        }

        void freeZlib(voidpf, voidpf buffer) {
            freeBuffer(buffer);
        }

        void reportContentProgress(const EpubConverter::Options& options, size_t itemIndex, size_t itemCount,
                                   uint32_t bytesRead, uint32_t totalBytes, size_t wordCount) {
            if (itemCount == 0 || totalBytes == 0) {
                return;
            }

            const uint32_t cappedBytes = std::min(bytesRead, totalBytes);
            const int contentPercent = static_cast<int>((cappedBytes * 100ULL) / totalBytes);
            const int itemPercent = static_cast<int>(((itemIndex * 100ULL) + contentPercent) / itemCount);
            const int progressPercent = 25 + ((itemPercent * 70) / 100);
            const std::string detail = std::to_string(itemIndex + 1) + "/" + std::to_string(itemCount) + " "
                                     + std::to_string(wordCount) + " words";
            reportProgress(options, "Extracting content", detail.c_str(), progressPercent);
        }

        bool seekToEntryPayload(File& file, const ZipEntry& entry, const char* context, uint32_t& dataOffset) {
            std::array<uint8_t, 30> localHeader;
            if (!file.seek(entry.localHeaderOffset)) {
                ESP_LOGE("epub-zip", "Could not seek to %s local header: %s offset=%lu", context, entry.name.c_str(),
                         static_cast<unsigned long>(entry.localHeaderOffset));
                return false;
            }
            if (!readExact(file, localHeader.data(), localHeader.size())) {
                ESP_LOGE("epub-zip", "Could not read %s local header: %s", context, entry.name.c_str());
                return false;
            }

            const uint32_t localSignature = readLe32(localHeader.data());
            if (localSignature != kZipLocalFileSignature) {
                ESP_LOGD("epub-zip", "Bad %s local signature for %s signature=0x%08lx", context, entry.name.c_str(),
                         static_cast<unsigned long>(localSignature));
                return false;
            }

            const uint16_t fileNameLength = readLe16(localHeader.data() + 26);
            const uint16_t extraLength = readLe16(localHeader.data() + 28);
            dataOffset = entry.localHeaderOffset + localHeader.size() + fileNameLength + extraLength;
            ESP_LOGD("epub-zip", "%s data: %s nameLen=%u extraLen=%u dataOffset=%lu", context, entry.name.c_str(),
                     fileNameLength, extraLength, static_cast<unsigned long>(dataOffset));
            if (!file.seek(dataOffset)) {
                ESP_LOGE("epub-zip", "Could not seek to %s data: %s offset=%lu", context, entry.name.c_str(),
                         static_cast<unsigned long>(dataOffset));
                return false;
            }

            return true;
        }

        template<typename ChunkSink>
        bool readStoredPayload(File& file, const ZipEntry& entry, uint32_t& totalOutputBytes, ChunkSink onChunk,
                               const char* context) {
            uint8_t* buffer = static_cast<uint8_t*>(allocateBuffer(kReadChunkBytes));
            if (buffer == nullptr) {
                ESP_LOGD("epub-zip", "No work buffer for stored %s: %s", context, entry.name.c_str());
                return false;
            }

            bool ok = true;
            uint32_t remaining = entry.uncompressedSize;
            while (remaining > 0) {
                const size_t chunk = std::min(kReadChunkBytes, static_cast<size_t>(remaining));
                if (!readExact(file, buffer, chunk)) {
                    ESP_LOGE("epub-zip", "Stored %s read failed: %s remaining=%lu", context, entry.name.c_str(),
                             static_cast<unsigned long>(remaining));
                    ok = false;
                    break;
                }
                if (!onChunk(buffer, chunk)) {
                    ok = false;
                    break;
                }

                totalOutputBytes += static_cast<uint32_t>(chunk);
                remaining -= static_cast<uint32_t>(chunk);
                serviceBackground();
            }

            freeBuffer(buffer);
            return ok;
        }

        template<typename ChunkSink>
        bool inflatePayload(File& file, const ZipEntry& entry, uint32_t& totalOutputBytes, ChunkSink onChunk,
                            const char* context) {
            uint8_t* inputBuffer = static_cast<uint8_t*>(allocateBuffer(kInflateInputChunkBytes));
            uint8_t* outputBuffer = static_cast<uint8_t*>(allocateBuffer(kReadChunkBytes));
            if (inputBuffer == nullptr || outputBuffer == nullptr) {
                ESP_LOGD("epub-zip", "No inflate buffers for %s: %s input=%s output=%s", context,
                         entry.name.c_str(), inputBuffer == nullptr ? "no" : "yes",
                         outputBuffer == nullptr ? "no" : "yes");
                freeBuffer(inputBuffer);
                freeBuffer(outputBuffer);
                return false;
            }

            z_stream stream{};
            stream.zalloc = allocateZlib;
            stream.zfree = freeZlib;
            int status = inflateInit2(&stream, -MAX_WBITS);
            if (status != Z_OK) {
                ESP_LOGE("epub-zip", "Could not initialize inflate for %s status=%d context=%s", entry.name.c_str(),
                         status, context);
                freeBuffer(inputBuffer);
                freeBuffer(outputBuffer);
                return false;
            }

            bool ok = true;
            uint32_t compressedRemaining = entry.compressedSize;

            while (status != Z_STREAM_END) {
                if (stream.avail_in == 0 && compressedRemaining > 0) {
                    const size_t chunk = std::min(kInflateInputChunkBytes, static_cast<size_t>(compressedRemaining));
                    if (!readExact(file, inputBuffer, chunk)) {
                        ESP_LOGE("epub-zip", "Deflated %s read failed: %s remaining=%lu", context, entry.name.c_str(),
                                 static_cast<unsigned long>(compressedRemaining));
                        ok = false;
                        break;
                    }

                    compressedRemaining -= static_cast<uint32_t>(chunk);
                    stream.next_in = inputBuffer;
                    stream.avail_in = static_cast<uInt>(chunk);
                }

                stream.next_out = outputBuffer;
                stream.avail_out = kReadChunkBytes;
                status = inflate(&stream, Z_NO_FLUSH);
                const size_t outputBytes = kReadChunkBytes - stream.avail_out;

                if (outputBytes > 0) {
                    if (!onChunk(outputBuffer, outputBytes)) {
                        ok = false;
                        break;
                    }
                    totalOutputBytes += static_cast<uint32_t>(outputBytes);
                }

                serviceBackground();

                if (status != Z_OK && status != Z_STREAM_END) {
                    ESP_LOGE("epub-zip", "Inflate failed for %s status=%d context=%s", entry.name.c_str(),
                             status, context);
                    ok = false;
                    break;
                }

                if (outputBytes == 0 && status != Z_STREAM_END && stream.avail_in == 0 && compressedRemaining == 0) {
                    ESP_LOGE("epub-zip", "Inflate stalled for %s status=%d context=%s", entry.name.c_str(),
                             status, context);
                    ok = false;
                    break;
                }
            }

            inflateEnd(&stream);
            freeBuffer(inputBuffer);
            freeBuffer(outputBuffer);
            return ok;
        }

    } // namespace

    bool Archive::open(std::string_view path) {
        archivePath_ = path;
        file_ = Board::Storage::filesystem().open(archivePath_.c_str());

        const auto failWithClosedArchive = [&]() {
            close();
            return false;
        };

        if (!file_ || file_.isDirectory()) {
            ESP_LOGE("epub-zip", "Open failed: %s", archivePath_.c_str());
            return failWithClosedArchive();
        }

        ESP_LOGI("epub-zip", "Opened archive: %s size=%lu", archivePath_.c_str(),
                 static_cast<unsigned long>(file_.size()));
        if (!readCentralDirectory()) {
            ESP_LOGE("epub-zip", "Central directory read failed: %s", archivePath_.c_str());
            return failWithClosedArchive();
        }
        ESP_LOGI("epub-zip", "Archive ready: %u file entries", static_cast<unsigned int>(entries_.size()));
        logArchiveHints("open");
        return true;
    }

    void Archive::close() {
        if (file_) {
            file_.close();
        }
        entries_.clear();
    }

    bool Archive::contains(std::string_view name) const {
        const std::string lowered = toLowerCopy(normalizeZipName(name));
        return std::ranges::any_of(entries_, [&](const ZipEntry& entry) {
            return toLowerCopy(entry.name) == lowered;
        });
    }

    const ZipEntry* Archive::find(std::string_view name) const {
        const std::string normalized = normalizeZipName(name);
        const auto exact = std::ranges::find(entries_, normalized, &ZipEntry::name);
        if (exact != entries_.end()) {
            return &(*exact);
        }

        const std::string lowered = toLowerCopy(normalized);
        const auto insensitive = std::ranges::find_if(entries_, [&](const ZipEntry& entry) {
            return toLowerCopy(entry.name) == lowered;
        });
        if (insensitive != entries_.end()) {
            ESP_LOGD("epub-zip", "Case-insensitive ZIP match: requested=%s actual=%s", normalized.c_str(),
                     insensitive->name.c_str());
            return &(*insensitive);
        }

        ESP_LOGE("epub-zip", "Entry not found: %s", normalized.c_str());
        logArchiveHints("missing entry");
        return nullptr;
    }

    bool Archive::extractToString(std::string_view name, std::string& output, size_t maxBytes) {
        ESP_LOGD("epub-zip", "Request string entry: %.*s", static_cast<int>(name.size()), name.data());
        const ZipEntry* entry = find(name);
        if (entry == nullptr) {
            return false;
        }
        return extractToString(*entry, output, maxBytes);
    }

    ContentExtractStatus Archive::extractContentToRsvp(std::string_view name, RsvpWriter& writer,
                                                       std::span<const EpubPackage::TocEntry> tocEntries, bool hasToc,
                                                       std::string_view fallbackChapterTitle,
                                                       std::string_view bookTitle,
                                                       const EpubConverter::Options& options, size_t itemIndex,
                                                       size_t itemCount) {
        const ZipEntry* entry = find(name);
        if (entry == nullptr) {
            ESP_LOGE("epub-zip", "Content entry not found: %.*s", static_cast<int>(name.size()), name.data());
            return ContentExtractStatus::Failed;
        }
        return extractContentToRsvp(*entry, writer, tocEntries, hasToc, fallbackChapterTitle, bookTitle, options,
                                    itemIndex, itemCount);
    }

    void Archive::logArchiveHints(const char* reason) const {
        ESP_LOGD("epub-zip", "Archive hints (%s): entries=%u", reason == nullptr ? "" : reason,
                 static_cast<unsigned int>(entries_.size()));

        auto logEntry = [](const char* label, size_t displayIndex, const ZipEntry& entry) {
            ESP_LOGD("epub-zip", "  %s[%u] %s method=%u flags=0x%04x c=%lu u=%lu local=%lu", label,
                     static_cast<unsigned int>(displayIndex), entry.name.c_str(), entry.method, entry.flags,
                     static_cast<unsigned long>(entry.compressedSize),
                     static_cast<unsigned long>(entry.uncompressedSize),
                     static_cast<unsigned long>(entry.localHeaderOffset));
        };

        size_t printed = 0;
        for (size_t i = 0; i < entries_.size() && printed < 10; ++i) {
            logEntry("entry", i, entries_[i]);
            ++printed;
        }

        size_t hinted = 0;
        for (size_t i = 0; i < entries_.size() && hinted < 20; ++i) {
            if (!isArchiveHintEntry(entries_[i].name)) {
                continue;
            }
            logEntry("hint", i, entries_[i]);
            ++hinted;
        }
    }

    bool Archive::readCentralDirectory() {
        const uint32_t fileSize = static_cast<uint32_t>(file_.size());
        if (fileSize < 22) {
            ESP_LOGD("epub-zip", "File too small for ZIP EOCD: %lu", static_cast<unsigned long>(fileSize));
            return false;
        }

        uint16_t entryCount = 0;
        uint32_t centralDirectoryOffset = 0;

        // Locate and validate the end-of-central-directory record before parsing
        // entries.
        {
            const size_t tailSize = fileSize < kZipEocdMaxSearch ? static_cast<size_t>(fileSize) : kZipEocdMaxSearch;
            uint8_t* tail = static_cast<uint8_t*>(allocateBuffer(tailSize));
            if (tail == nullptr) {
                ESP_LOGE("epub-zip", "No memory for EOCD tail buffer: %u bytes", static_cast<unsigned int>(tailSize));
                return false;
            }

            const uint32_t tailOffset = fileSize - static_cast<uint32_t>(tailSize);
            ESP_LOGD("epub-zip", "Searching EOCD: fileSize=%lu tailOffset=%lu tailSize=%u",
                     static_cast<unsigned long>(fileSize), static_cast<unsigned long>(tailOffset),
                     static_cast<unsigned int>(tailSize));
            const bool ok = file_.seek(tailOffset) && readExact(file_, tail, tailSize);
            const int eocdIndex = [&]() {
                if (!ok) {
                    return -1;
                }
                for (int i = static_cast<int>(tailSize) - 22; i >= 0; --i) {
                    if (readLe32(tail + i) == kZipEocdSignature) {
                        return i;
                    }
                }
                return -1;
            }();

            if (eocdIndex < 0) {
                ESP_LOGE("epub-zip", "EOCD signature not found (tailRead=%s)", ok ? "yes" : "no");
                freeBuffer(tail);
                return false;
            }

            const uint16_t diskNumber = readLe16(tail + eocdIndex + 4);
            const uint16_t directoryDisk = readLe16(tail + eocdIndex + 6);
            entryCount = readLe16(tail + eocdIndex + 10);
            centralDirectoryOffset = readLe32(tail + eocdIndex + 16);
            const uint32_t centralDirectorySize = readLe32(tail + eocdIndex + 12);
            freeBuffer(tail);

            ESP_LOGD("epub-zip", "EOCD found: eocdOffset=%lu entries=%u cdOffset=%lu cdSize=%lu disk=%u dirDisk=%u",
                     static_cast<unsigned long>(tailOffset + static_cast<uint32_t>(eocdIndex)), entryCount,
                     static_cast<unsigned long>(centralDirectoryOffset),
                     static_cast<unsigned long>(centralDirectorySize), diskNumber, directoryDisk);

            if (diskNumber != 0 || directoryDisk != 0 || entryCount == 0 || entryCount > kMaxZipEntries) {
                ESP_LOGW("epub", "Unsupported ZIP directory entry count: %u", entryCount);
                return false;
            }
        }

        entries_.clear();
        entries_.reserve(entryCount);
        if (!file_.seek(centralDirectoryOffset)) {
            ESP_LOGE("epub-zip", "Could not seek to central directory offset=%lu",
                     static_cast<unsigned long>(centralDirectoryOffset));
            return false;
        }

        for (uint16_t i = 0; i < entryCount; ++i) {
            if ((i & 0x1F) == 0) {
                serviceBackground();
            }

            ZipEntry entry;
            uint16_t extraLength = 0;
            uint16_t commentLength = 0;

            // Keep the fixed header and temporary filename buffer scoped to one entry.
            {
                std::array<uint8_t, 46> header;
                if (!readExact(file_, header.data(), header.size())
                    || readLe32(header.data()) != kZipCentralFileSignature) {
                    ESP_LOGD("epub-zip", "Bad central header at index=%u pos=%lu", i,
                             static_cast<unsigned long>(file_.position()));
                    return false;
                }

                const uint16_t fileNameLength = readLe16(header.data() + 28);
                extraLength = readLe16(header.data() + 30);
                commentLength = readLe16(header.data() + 32);
                if (fileNameLength == 0 || fileNameLength > kMaxZipNameLength) {
                    ESP_LOGW("epub", "Unsupported ZIP filename length: %u", fileNameLength);
                    return false;
                }

                char* nameBuffer = static_cast<char*>(allocateBuffer(fileNameLength + 1));
                if (nameBuffer == nullptr) {
                    ESP_LOGE("epub-zip", "No memory for filename buffer: %u bytes", fileNameLength + 1);
                    return false;
                }

                const bool nameRead = readExact(file_, reinterpret_cast<uint8_t*>(nameBuffer), fileNameLength);
                nameBuffer[fileNameLength] = '\0';

                entry.name = normalizeZipName(nameBuffer);
                entry.method = readLe16(header.data() + 10);
                entry.flags = readLe16(header.data() + 8);
                entry.compressedSize = readLe32(header.data() + 20);
                entry.uncompressedSize = readLe32(header.data() + 24);
                entry.localHeaderOffset = readLe32(header.data() + 42);
                freeBuffer(nameBuffer);

                if (!nameRead) {
                    return false;
                }
            }

            const uint32_t nextPosition = static_cast<uint32_t>(file_.position()) + extraLength + commentLength;
            if (!file_.seek(nextPosition)) {
                ESP_LOGE("epub-zip", "Could not seek past central extras for %s next=%lu", entry.name.c_str(),
                         static_cast<unsigned long>(nextPosition));
                return false;
            }

            if (!entry.name.ends_with('/')) {
                entries_.push_back(entry);
            }
        }

        ESP_LOGD("epub-zip", "Central directory parsed: kept=%u rawEntries=%u",
                 static_cast<unsigned int>(entries_.size()), entryCount);
        return true;
    }

    bool Archive::extractToString(const ZipEntry& entry, std::string& output, size_t maxBytes) {
        output.clear();

        ESP_LOGD("epub-zip", "Extract string: %s method=%u flags=0x%04x c=%lu u=%lu max=%u", entry.name.c_str(),
                 entry.method, entry.flags, static_cast<unsigned long>(entry.compressedSize),
                 static_cast<unsigned long>(entry.uncompressedSize), static_cast<unsigned int>(maxBytes));

        if (entry.uncompressedSize == 0 || entry.uncompressedSize > maxBytes || entry.compressedSize == 0
            || entry.compressedSize > maxBytes) {
            ESP_LOGW("epub", "Skipping %s (%lu compressed, %lu uncompressed bytes)", entry.name.c_str(),
                     static_cast<unsigned long>(entry.compressedSize),
                     static_cast<unsigned long>(entry.uncompressedSize));
            return false;
        }

        uint32_t dataOffset = 0;
        if (!seekToEntryPayload(file_, entry, "string", dataOffset)) {
            return false;
        }

        output.reserve(entry.uncompressedSize);

        uint32_t totalOutputBytes = 0;
        auto appendBytes = [&](const uint8_t* data, size_t length) -> bool {
            if (length == 0) {
                return true;
            }
            if (totalOutputBytes + length > maxBytes) {
                ESP_LOGE("epub-zip", "Text extraction exceeded limit for %s", entry.name.c_str());
                return false;
            }
            output.append(reinterpret_cast<const char*>(data), length);
            return true;
        };

        bool ok = [&]() {
            if (entry.method == kZipStored) {
                ESP_LOGD("epub-zip", "Reading stored string payload: %s", entry.name.c_str());
                return readStoredPayload(file_, entry, totalOutputBytes, appendBytes, "string");
            }
            if (entry.method == kZipDeflated) {
                ESP_LOGD("epub-zip", "Streaming inflate string payload: %s", entry.name.c_str());
                return inflatePayload(file_, entry, totalOutputBytes, appendBytes, "string");
            }
            ESP_LOGW("epub", "Unsupported ZIP method %u for %s", entry.method, entry.name.c_str());
            return false;
        }();

        if (ok && totalOutputBytes != entry.uncompressedSize) {
            ESP_LOGE("epub-zip", "Text inflate size mismatch for %s (%lu of %lu bytes)", entry.name.c_str(),
                     static_cast<unsigned long>(totalOutputBytes), static_cast<unsigned long>(entry.uncompressedSize));
            ok = false;
        }

        if (ok) {
            ESP_LOGD("epub-zip", "Extracted string OK: %s textLen=%u", entry.name.c_str(),
                     static_cast<unsigned int>(output.length()));
        }

        return ok;
    }

    ContentExtractStatus Archive::extractContentToRsvp(const ZipEntry& entry, RsvpWriter& rsvpWriter,
                                                       std::span<const EpubPackage::TocEntry> tocEntries, bool hasToc,
                                                       std::string_view fallbackChapterTitle,
                                                       std::string_view bookTitle,
                                                       const EpubConverter::Options& options, size_t itemIndex,
                                                       size_t itemCount) {
        ESP_LOGD("epub-zip", "Extract content: %s method=%u flags=0x%04x c=%lu u=%lu", entry.name.c_str(), entry.method,
                 entry.flags, static_cast<unsigned long>(entry.compressedSize),
                 static_cast<unsigned long>(entry.uncompressedSize));

        if (entry.uncompressedSize == 0 || entry.compressedSize == 0 || entry.uncompressedSize > options.maxContentBytes
            || entry.compressedSize > options.maxContentBytes) {
            ESP_LOGW("epub", "Skipping oversized content %s (%lu compressed, %lu uncompressed bytes)",
                     entry.name.c_str(), static_cast<unsigned long>(entry.compressedSize),
                     static_cast<unsigned long>(entry.uncompressedSize));
            return ContentExtractStatus::Unsupported;
        }

        uint32_t dataOffset = 0;
        if (!seekToEntryPayload(file_, entry, "content", dataOffset)) {
            return ContentExtractStatus::Failed;
        }

        EpubContent::Parser parser(rsvpWriter, tocEntries, hasToc, fallbackChapterTitle, bookTitle);
        uint32_t totalOutputBytes = 0;
        uint32_t lastProgressBytes = 0;
        ContentExtractStatus result = ContentExtractStatus::Complete;

        auto finishWriter = [&]() -> ContentExtractStatus {
            if (!parser.finish()) {
                return parser.reachedWordLimit() ? ContentExtractStatus::WordLimitReached
                                                 : ContentExtractStatus::Failed;
            }
            return ContentExtractStatus::Complete;
        };

        auto reportMaybe = [&](bool force) {
            if (!force && totalOutputBytes - lastProgressBytes < 32UL * 1024UL) {
                return;
            }
            lastProgressBytes = totalOutputBytes;
            reportContentProgress(options, itemIndex, itemCount, totalOutputBytes, entry.uncompressedSize,
                                  rsvpWriter.wordCount());
        };

        auto writeChunk = [&](const uint8_t* data, size_t length) -> bool {
            if (!parser.write(data, length)) {
                result =
                    parser.reachedWordLimit() ? ContentExtractStatus::WordLimitReached : ContentExtractStatus::Failed;
                return false;
            }
            reportMaybe(false);
            return true;
        };

        const auto extractPayload = [&]() {
            if (entry.method == kZipStored) {
                return readStoredPayload(file_, entry, totalOutputBytes, writeChunk, "content");
            }
            if (entry.method == kZipDeflated) {
                return inflatePayload(file_, entry, totalOutputBytes, writeChunk, "content");
            }
            ESP_LOGW("epub", "Unsupported ZIP method %u for %s", entry.method, entry.name.c_str());
            result = ContentExtractStatus::Unsupported;
            return false;
        };

        const bool ok = extractPayload();

        if (!ok) {
            return result == ContentExtractStatus::Complete ? ContentExtractStatus::Failed : result;
        }

        if (totalOutputBytes != entry.uncompressedSize) {
            ESP_LOGE("epub", "Inflate size mismatch for %s (%lu of %lu bytes)", entry.name.c_str(),
                     static_cast<unsigned long>(totalOutputBytes), static_cast<unsigned long>(entry.uncompressedSize));
            return ContentExtractStatus::Failed;
        }

        reportMaybe(true);
        return finishWriter();
    }

} // namespace EpubZip
