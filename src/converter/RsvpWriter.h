#pragma once

#include <FS.h>
#include <cstddef>
#include <string>
#include <string_view>

#include "storage/fs/BufferedWriter.h"

class RsvpWriter {
public:
    struct Metadata {
        std::string_view source;
        std::string_view title;
        std::string_view author;
        std::string_view converter;
        std::string_view language;
        bool verticalWriting = false;
    };

    RsvpWriter(File& output, const Metadata& metadata, size_t maxWords = 0);

    bool writeText(std::string_view text, bool endsParagraph = true);
    bool writeChapter(std::string_view title);
    bool setLanguage(std::string_view language);
    bool setDirection(std::string_view direction);
    bool setVerticalWriting();
    bool finish();
    void endParagraph() noexcept;

    [[nodiscard]] size_t wordCount() const noexcept;
    [[nodiscard]] size_t chapterCount() const noexcept;
    [[nodiscard]] bool reachedWordLimit() const noexcept;
    [[nodiscard]] std::string_view language() const noexcept;
    [[nodiscard]] std::string_view direction() const noexcept;

private:
    bool writeHeader(const Metadata& metadata);
    bool beginParagraph();
    bool writeLine(std::string_view line = {});
    bool writePrefixedLine(std::string_view prefix, std::string_view value);

    BufferedWriter output_;
    size_t maxWords_ = 0;
    size_t wordCount_ = 0;
    size_t chapterCount_ = 0;
    std::string lastChapterTitle_;
    std::string outputLine_;
    std::string language_ = "und";
    std::string direction_ = "auto";
    bool paragraphOpen_ = false;
    bool verticalWriting_ = false;
    bool reachedWordLimit_ = false;
    bool failed_ = false;
    bool finished_ = false;
};
