#pragma once

#include <Arduino.h>
#include <FS.h>
#include <span>

#include "converter/EpubPackage.h"

namespace EpubContent {

    String plainTextFromXmlFragment(const String& fragment);
    bool writeBodyLine(File& output, const String& line, size_t& wordCount, size_t maxWords);

    class RsvpContentWriter {
    public:
        RsvpContentWriter(File& output, size_t& wordCount, size_t maxWords, String& lastChapterTitle,
                          size_t& chapterCount, std::span<const EpubPackage::TocEntry> tocEntries, bool hasToc,
                          String fallbackChapterTitle, String bookTitle);

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
        bool writeChapter(const String& title);
        bool emitTocEntriesThrough(size_t index);
        int matchingTocEntry(const String& anchor) const;
        bool suppressHeading(const String& heading) const;
        void beginParagraph();
        void appendToActiveText(char c);
        bool processDecodedText(char c);
        bool processTextChar(char c);
        bool processTag(const String& tag);
        bool processEntityChar(char c);
        bool processCommentChar(char c);
        bool processChar(char c);

        File& output_;
        size_t& wordCount_;
        const size_t maxWords_;
        String& lastChapterTitle_;
        size_t& chapterCount_;
        const std::span<const EpubPackage::TocEntry> tocEntries_;
        const bool hasToc_;
        const String fallbackChapterTitle_;
        const String bookTitle_;
        String line_;
        String heading_;
        String tag_;
        String entity_;
        String commentTail_;
        Mode mode_ = Mode::Text;
        bool inHeading_ = false;
        bool reachedWordLimit_ = false;
        bool paragraphOpen_ = false;
        bool documentChapterWritten_ = false;
        size_t nextTocEntry_ = 0;
        int skipDepth_ = 0;
    };

} // namespace EpubContent
