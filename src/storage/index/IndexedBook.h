#pragma once

#include <array>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include "book/BookMetadata.h"
#include "storage/fs/BufferedWriter.h"
#include "storage/index/IndexedBookStore.h"
#include "storage/library/BookLibrary.h"
#include "text/RsvpTokenizer.h"

namespace IndexedBook {

    using StatusCallback = void (*)(void* context, const char* title, const char* line1, const char* line2,
                                    int progressPercent);

    struct OpenRequest {
        bool allowIndexBuild = true;
        bool allowDocumentConversion = true;
        StatusCallback statusCallback = nullptr;
        void* statusContext = nullptr;
    };

    class Builder {
    public:
        Builder(fs::FS& filesystem, std::string_view sourcePath, uint32_t sourceSize, bool rsvpFormat);
        ~Builder();

        Builder(const Builder&) = delete;
        Builder& operator=(const Builder&) = delete;

        std::expected<void, std::error_code> begin();
        std::expected<void, std::error_code> append(std::span<const uint8_t> bytes);
        std::expected<void, std::error_code> finish();
        std::expected<void, std::error_code> commit();

        [[nodiscard]] const IndexedBookStore::Header& header() const noexcept;
        [[nodiscard]] BookMetadata takeMetadata();
        [[nodiscard]] const char* failure() const noexcept;

    private:
        std::expected<void, std::error_code> fail(std::error_code error, const char* detail);
        bool processLine(std::string_view line);
        bool processBookLine(std::string_view line);
        bool processRsvpLine(std::string_view line);
        bool appendLineWords(std::string_view line);
        bool pushWord(std::string token);
        void addChapter(std::string_view title);
        void addParagraph();
        void addTextRun();
        void updateFingerprint(std::span<const uint8_t> bytes);
        uint32_t fingerprint() const;
        void cleanup() noexcept;

        fs::FS& filesystem_;
        std::string temporaryIndexPath_;
        std::string temporaryDataPath_;
        File indexFile_;
        File dataFile_;
        BufferedWriter indexWriter_;
        BufferedWriter dataWriter_;
        BookMetadata metadata_;
        IndexedBookStore::Header header_;
        RsvpText::ParseStats stats_;
        std::string line_;
        std::string locale_;
        std::array<uint32_t, 3> sampleOffsets_{};
        std::array<uint32_t, 3> sampleHashes_{};
        uint32_t sourceSize_ = 0;
        uint32_t receivedBytes_ = 0;
        size_t wordCount_ = 0;
        uint32_t dataSize_ = 0;
        TextDirection direction_ = TextDirection::automatic;
        std::error_code error_;
        const char* failure_ = "";
        bool rsvpFormat_ = false;
        bool paragraphPending_ = true;
        bool parsingComplete_ = false;
        bool begun_ = false;
        bool finished_ = false;
        bool committed_ = false;
    };

    bool load(size_t index, BookLibrary::Listing& library, IndexedBookStore& store, BookMetadata& metadata,
              const OpenRequest& request);
    bool readMetadata(std::string_view path, BookMetadata& metadata, IndexedBookStore::Header* headerOut = nullptr);

} // namespace IndexedBook
