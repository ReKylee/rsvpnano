#pragma once

#include <FS.h>
#include <span>
#include <string>
#include <string_view>

#include "converter/EpubPackage.h"

namespace EpubContent {

    std::string plainTextFromXmlFragment(std::string_view fragment);
    bool writeBodyLine(File& output, std::string_view line, size_t& wordCount, size_t maxWords);

    class RsvpContentWriter {
    public:
        RsvpContentWriter(File& output, size_t& wordCount, size_t maxWords, std::string& lastChapterTitle,
                          size_t& chapterCount, std::span<const EpubPackage::TocEntry> tocEntries, bool hasToc,
                          std::string_view fallbackChapterTitle, std::string_view bookTitle);

        bool write(const uint8_t* data, size_t length);
        bool finish();
        bool reachedWordLimit() const;

    private:
        enum class Mode {
            Text,
            Tag,
            Entity,
            Comment,
        };

        bool flushLine(bool endParagraph = true);
        bool flushWordAlignedPrefix();
        bool writeChapter(std::string_view title);
        bool emitTocEntriesThrough(size_t index);
        int matchingTocEntry(std::string_view anchor) const;
        bool suppressHeading(std::string_view heading) const;
        void beginParagraph();
        void appendToActiveText(char c);
        bool processDecodedText(char c);
        bool processTextChar(char c);
        bool processTag(std::string_view tag);
        bool processEntityChar(char c);
        bool processCommentChar(char c);
        bool processChar(char c);

        File& output_;
        size_t& wordCount_;
        const size_t maxWords_;
        std::string& lastChapterTitle_;
        size_t& chapterCount_;
        const std::span<const EpubPackage::TocEntry> tocEntries_;
        const bool hasToc_;
        const std::string fallbackChapterTitle_;
        const std::string bookTitle_;
        std::string line_;
        std::string heading_;
        std::string tag_;
        std::string entity_;
        std::string commentTail_;
        Mode mode_ = Mode::Text;
        bool inHeading_ = false;
        bool reachedWordLimit_ = false;
        bool paragraphOpen_ = false;
        bool documentChapterWritten_ = false;
        size_t nextTocEntry_ = 0;
        int skipDepth_ = 0;
    };

} // namespace EpubContent
