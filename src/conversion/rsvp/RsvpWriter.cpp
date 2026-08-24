#include "conversion/rsvp/RsvpWriter.h"

#include <algorithm>
#include <string>

#include "conversion/rsvp/RsvpFormat.h"
#include "text/AsciiText.h"
#include "text/RsvpTokenizer.h"
#include "text/TextNormalizer.h"
#include "text/UnicodeText.h"
#include "text/Utf8Text.h"

namespace {

    constexpr size_t kOutputWrapWidth = 96;
    constexpr size_t kMaxChapterTitleBytes = 256;
    constexpr size_t kMaxAuthorBytes = 256;
    constexpr size_t kMaxSourceBytes = 512;
    constexpr size_t kMaxLanguageBytes = 64;

    std::string boundedDisplayText(std::string_view text, size_t maxBytes) {
        std::string result = RsvpText::normalizeDisplayText(text);
        if (result.size() > maxBytes)
            result.resize(Utf8Text::prefix(result, maxBytes).size());
        return std::string{AsciiText::trim(result)};
    }

} // namespace

RsvpWriter::RsvpWriter(File& output, const Metadata& metadata, size_t maxWords) : output_(output), maxWords_(maxWords) {
    outputLine_.reserve(kOutputWrapWidth);
    failed_ = !writeHeader(metadata);
}

bool RsvpWriter::writeHeader(const Metadata& metadata) {
    std::string title = boundedDisplayText(metadata.title, kMaxChapterTitleBytes);
    if (title.empty())
        title = "Untitled";
    if (!writePrefixedLine("@rsvp ", RsvpFormat::kVersion)
        || !writePrefixedLine("@title ", title)
        || !writePrefixedLine("@source ", boundedDisplayText(metadata.source, kMaxSourceBytes)))
        return false;
    const std::string author = boundedDisplayText(metadata.author, kMaxAuthorBytes);
    if (!author.empty() && !writePrefixedLine("@author ", author))
        return false;
    if (!metadata.converter.empty() && !writePrefixedLine("@converter ", metadata.converter))
        return false;
    if (!metadata.language.empty()) {
        language_ = boundedDisplayText(metadata.language, kMaxLanguageBytes);
        if (!language_.empty() && !writePrefixedLine("@language ", language_))
            return false;
        if (language_.empty())
            language_ = "und";
    }
    if (metadata.verticalWriting) {
        verticalWriting_ = true;
        if (!writeLine("@writing-mode vertical-rl"))
            return false;
    }
    return writeLine();
}

bool RsvpWriter::writeText(std::string_view text, bool endsParagraph) {
    if (failed_ || finished_ || reachedWordLimit_)
        return false;

    const std::string normalizedLine = RsvpText::normalizeDisplayText(text);
    if (normalizedLine.empty()) {
        if (endsParagraph)
            endParagraph();
        return true;
    }

    if (!beginParagraph())
        return false;
    outputLine_.clear();
    bool previousCjk = false;

    auto flushOutputLine = [&]() {
        if (outputLine_.empty())
            return true;
        if (outputLine_.starts_with('@') && !output_.write("@"))
            return false;
        if (!writeLine(outputLine_))
            return false;
        outputLine_.clear();
        previousCjk = false;
        return true;
    };

    auto consumeRsvpToken = [&](const std::string& value) {
        if (value.empty())
            return true;

        const bool cjk = UnicodeText::isCjkText(value);
        const bool separated = !outputLine_.empty() && !(previousCjk && cjk);
        if (outputLine_.length() + value.length() + separated > kOutputWrapWidth && !flushOutputLine())
            return false;

        if (!outputLine_.empty() && !(previousCjk && cjk))
            outputLine_ += ' ';
        outputLine_ += value;
        previousCjk = cjk;
        if (RsvpText::hasReadableText(value))
            ++wordCount_;
        return true;
    };

    const bool keepGoing =
        RsvpText::appendNormalizedLineWords(normalizedLine, consumeRsvpToken, wordCount_, maxWords_);
    if (!flushOutputLine()) {
        failed_ = true;
        return false;
    }
    if (endsParagraph)
        endParagraph();
    reachedWordLimit_ = !keepGoing;
    return keepGoing;
}

bool RsvpWriter::writeChapter(std::string_view title) {
    if (failed_ || finished_)
        return false;
    const std::string cleaned = boundedDisplayText(title, kMaxChapterTitleBytes);
    if (cleaned.empty() || cleaned == lastChapterTitle_)
        return true;
    if ((wordCount_ > 0 || !lastChapterTitle_.empty()) && !writeLine())
        return false;
    if (!writePrefixedLine("@chapter ", cleaned))
        return false;
    lastChapterTitle_ = cleaned;
    ++chapterCount_;
    endParagraph();
    return true;
}

bool RsvpWriter::setLanguage(std::string_view language) {
    if (failed_ || finished_)
        return false;
    const std::string next = language.empty() ? "und" : std::string{language};
    if (next == language_)
        return true;
    if (!writePrefixedLine("@language ", next))
        return false;
    language_ = next;
    return true;
}

bool RsvpWriter::setDirection(std::string_view direction) {
    if (failed_ || finished_)
        return false;
    const std::string next = direction == "ltr" || direction == "rtl" ? std::string{direction} : "auto";
    if (next == direction_)
        return true;
    if (!writePrefixedLine("@direction ", next))
        return false;
    direction_ = next;
    return true;
}

bool RsvpWriter::setVerticalWriting() {
    if (failed_ || finished_)
        return false;
    if (verticalWriting_)
        return true;
    if (!writeLine("@writing-mode vertical-rl"))
        return false;
    verticalWriting_ = true;
    return true;
}

bool RsvpWriter::finish() {
    if (finished_)
        return !failed_;
    finished_ = true;
    if (failed_)
        return false;
    failed_ = !output_.flush();
    return !failed_;
}

void RsvpWriter::endParagraph() noexcept {
    paragraphOpen_ = false;
}

size_t RsvpWriter::wordCount() const noexcept {
    return wordCount_;
}

size_t RsvpWriter::chapterCount() const noexcept {
    return chapterCount_;
}

bool RsvpWriter::reachedWordLimit() const noexcept {
    return reachedWordLimit_;
}

std::string_view RsvpWriter::language() const noexcept {
    return language_;
}

std::string_view RsvpWriter::direction() const noexcept {
    return direction_;
}

bool RsvpWriter::beginParagraph() {
    if (paragraphOpen_)
        return true;
    if (wordCount_ > 0) {
        if (!writeLine() || !writeLine("@para"))
            return false;
    }
    paragraphOpen_ = true;
    return true;
}

bool RsvpWriter::writeLine(std::string_view line) {
    if (failed_)
        return false;
    if (output_.writeLine(line))
        return true;
    failed_ = true;
    return false;
}

bool RsvpWriter::writePrefixedLine(std::string_view prefix, std::string_view value) {
    if (failed_ || !output_.write(prefix) || !output_.writeLine(value)) {
        failed_ = true;
        return false;
    }
    return true;
}
