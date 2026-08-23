#pragma once

#include <FS.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "converter/EpubConverter.h"
#include "converter/EpubPackage.h"

namespace EpubZip {

    enum class ContentExtractStatus {
        Complete,
        WordLimitReached,
        Unsupported,
        Failed,
    };

    struct ZipEntry {
        std::string name;
        uint16_t method = 0;
        uint16_t flags = 0;
        uint32_t compressedSize = 0;
        uint32_t uncompressedSize = 0;
        uint32_t localHeaderOffset = 0;
    };

    class Archive {
    public:
        bool open(std::string_view path);
        void close();
        std::span<const ZipEntry> entries() const {
            return entries_;
        }
        bool contains(std::string_view name) const;
        const ZipEntry* find(std::string_view name) const;

        bool extractToString(std::string_view name, std::string& output, size_t maxBytes);
        ContentExtractStatus extractContentToRsvp(std::string_view name, File& output, size_t& wordCount,
                                                  size_t maxWords, std::string& lastChapterTitle, size_t& chapterCount,
                                                  std::span<const EpubPackage::TocEntry> tocEntries, bool hasToc,
                                                  std::string_view fallbackChapterTitle, std::string_view bookTitle,
                                                  std::string_view bookLocale, bool& verticalWritingEmitted,
                                                  const EpubConverter::Options& options, size_t itemIndex,
                                                  size_t itemCount);

    private:
        void logArchiveHints(const char* reason) const;
        bool readCentralDirectory();
        bool extractToString(const ZipEntry& entry, std::string& output, size_t maxBytes);
        ContentExtractStatus extractContentToRsvp(const ZipEntry& entry, File& output, size_t& wordCount,
                                                  size_t maxWords, std::string& lastChapterTitle, size_t& chapterCount,
                                                  std::span<const EpubPackage::TocEntry> tocEntries, bool hasToc,
                                                  std::string_view fallbackChapterTitle, std::string_view bookTitle,
                                                  std::string_view bookLocale, bool& verticalWritingEmitted,
                                                  const EpubConverter::Options& options, size_t itemIndex,
                                                  size_t itemCount);

        std::string archivePath_;
        File file_;
        std::vector<ZipEntry> entries_;
    };

} // namespace EpubZip
