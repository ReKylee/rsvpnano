#include "converter/EpubContentWriter.h"

#include <algorithm>
#include <array>
#include <utility>

#include "text/AsciiText.h"
#include "text/RsvpTokenizer.h"
#include "text/TextNormalizer.h"

namespace EpubContent {
    namespace {

        constexpr size_t kMaxEntityChars = 16;
        constexpr size_t kMaxTagChars = 512;
        constexpr size_t kOutputWrapWidth = 96;
        constexpr size_t kBufferedTextFlushThreshold = 220;

        struct TagInfo {
            String name;
            String anchor;
            bool closing = false;
            bool selfClosing = false;
        };

        constexpr bool isAsciiTagNameChar(char c) {
            return AsciiText::isAlphaNumeric(c) || c == ':' || c == '-' || c == '_';
        }

        void serviceBackground() {
            yield();
            delay(0);
        }

        String decodedEntityText(const String& entity) {
            String decoded;
            return RsvpText::decodeMarkupEntity(entity, decoded) ? decoded : " ";
        }

        void appendNormalizedChar(String& target, char c) {
            if (AsciiText::isWhitespace(c)) {
                if (!target.isEmpty() && target[target.length() - 1] != ' ') {
                    target += ' ';
                }
                return;
            }

            target += c;
        }

        bool hasReadableText(const String& token) {
            const char* text = token.c_str();
            return std::any_of(text, text + token.length(), [](char c) {
                return RsvpText::isReadableTokenChar(c);
            });
        }

        String tagAttributeValue(const String& tag, const char* name) {
            const String key(name);
            int position = 0;
            while ((position = tag.indexOf(key, position)) >= 0) {
                const bool boundaryBefore = position == 0 || AsciiText::isWhitespace(tag[position - 1])
                                         || tag[position - 1] == '<' || tag[position - 1] == '/';
                int afterName = position + key.length();
                const bool boundaryAfter = static_cast<size_t>(afterName) >= tag.length()
                                        || AsciiText::isWhitespace(tag[afterName]) || tag[afterName] == '=';
                if (!boundaryBefore || !boundaryAfter) {
                    position = afterName;
                    continue;
                }
                while (static_cast<size_t>(afterName) < tag.length() && AsciiText::isWhitespace(tag[afterName])) {
                    ++afterName;
                }
                if (static_cast<size_t>(afterName) >= tag.length() || tag[afterName] != '=') {
                    position = afterName;
                    continue;
                }
                do {
                    ++afterName;
                } while (static_cast<size_t>(afterName) < tag.length() && AsciiText::isWhitespace(tag[afterName]));
                if (static_cast<size_t>(afterName) >= tag.length()) {
                    return "";
                }

                const char quote = tag[afterName];
                if (quote == '"' || quote == '\'') {
                    const int end = tag.indexOf(quote, afterName + 1);
                    return end < 0 ? String("") : tag.substring(afterName + 1, end);
                }
                int end = afterName;
                while (static_cast<size_t>(end) < tag.length() && !AsciiText::isWhitespace(tag[end])
                       && tag[end] != '>') {
                    ++end;
                }
                return tag.substring(afterName, end);
            }
            return "";
        }

        TagInfo parseTagInfo(const String& tag) {
            TagInfo info;

            size_t position = 1;
            while (position < tag.length() && AsciiText::isWhitespace(tag[position])) {
                ++position;
            }
            if (position < tag.length() && tag[position] == '/') {
                info.closing = true;
                ++position;
            }
            while (position < tag.length() && AsciiText::isWhitespace(tag[position])) {
                ++position;
            }

            const size_t start = position;
            while (position < tag.length() && isAsciiTagNameChar(tag[position])) {
                ++position;
            }

            info.name = tag.substring(start, position);
            info.name.toLowerCase();
            info.anchor = tagAttributeValue(tag, "id");
            if (info.anchor.isEmpty()) {
                info.anchor = tagAttributeValue(tag, "name");
            }
            info.anchor.trim();

            for (int i = static_cast<int>(tag.length()) - 1; i >= 0; --i) {
                if (AsciiText::isWhitespace(tag[i]) || tag[i] == '>') {
                    continue;
                }
                info.selfClosing = tag[i] == '/';
                break;
            }

            return info;
        }

        bool isSkipTag(const String& name) {
            static constexpr std::array<const char*, 6> kSkippedTags = {{
                "head",
                "script",
                "style",
                "svg",
                "math",
                "nav",
            }};
            return std::any_of(kSkippedTags.begin(), kSkippedTags.end(), [&](const char* tag) {
                return name == tag;
            });
        }

        bool isHeadingTag(const String& name) {
            return name.length() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6';
        }

        bool isBlockTag(const String& name) {
            static constexpr std::array<const char*, 31> kBlockTags = {{
                "address",    "article", "aside",  "blockquote", "body",  "br", "dd",    "div", "dl", "dt",
                "figcaption", "figure",  "footer", "header",     "hr",    "li", "main",  "ol",  "p",  "pre",
                "section",    "table",   "tbody",  "td",         "tfoot", "th", "thead", "tr",  "ul",
            }};
            return std::any_of(kBlockTags.begin(), kBlockTags.end(), [&](const char* tag) {
                return name == tag;
            });
        }

        String normalizedLabel(String value) {
            value = RsvpText::normalizeDisplayText(value);
            String normalized;
            normalized.reserve(value.length());
            for (size_t i = 0; i < value.length(); ++i) {
                if (RsvpText::isReadableTokenChar(value[i])) {
                    normalized += value[i];
                }
            }
            normalized.toLowerCase();
            return normalized;
        }

    } // namespace

    String plainTextFromXmlFragment(const String& fragment) {
        String text;
        text.reserve(std::min<size_t>(fragment.length(), 160));

        for (size_t i = 0; i < fragment.length(); ++i) {
            const char c = fragment[i];
            if (c == '<') {
                const int tagEnd = fragment.indexOf('>', i + 1);
                if (tagEnd < 0) {
                    break;
                }
                i = tagEnd;
                appendNormalizedChar(text, ' ');
                continue;
            }

            if (c == '&') {
                const int entityEnd = fragment.indexOf(';', i + 1);
                const int entityLength = entityEnd - static_cast<int>(i) - 1;
                if (entityEnd > 0 && entityLength >= 0 && entityLength <= static_cast<int>(kMaxEntityChars)) {
                    const String decoded = decodedEntityText(fragment.substring(i + 1, entityEnd));
                    std::for_each(decoded.c_str(), decoded.c_str() + decoded.length(), [&](char decodedChar) {
                        appendNormalizedChar(text, decodedChar);
                    });
                    i = entityEnd;
                    continue;
                }
            }

            appendNormalizedChar(text, c);
        }

        text.trim();
        return RsvpText::normalizeDisplayText(text);
    }

    bool writeBodyLine(File& output, const String& line, size_t& wordCount, size_t maxWords) {
        const String normalizedLine = RsvpText::normalizeDisplayText(line);
        String outputLine;

        auto flushOutputLine = [&]() {
            if (outputLine.isEmpty()) {
                return;
            }
            if (outputLine.startsWith("@")) {
                output.print('@');
            }
            output.println(outputLine);
            outputLine = "";
        };

        auto consumeRsvpToken = [&](const String& value) {
            if (value.isEmpty()) {
                return true;
            }

            if (outputLine.length() + value.length() + 1 > kOutputWrapWidth) {
                flushOutputLine();
            }

            if (!outputLine.isEmpty()) {
                outputLine += ' ';
            }
            outputLine += value;
            if (hasReadableText(value)) {
                ++wordCount;
            }
            return true;
        };

        const bool keepGoing =
            RsvpText::appendNormalizedLineWords(normalizedLine, consumeRsvpToken, wordCount, maxWords);

        flushOutputLine();
        return keepGoing;
    }

    RsvpContentWriter::RsvpContentWriter(File& output, size_t& wordCount, size_t maxWords, String& lastChapterTitle,
                                         size_t& chapterCount, std::span<const EpubPackage::TocEntry> tocEntries,
                                         bool hasToc, String fallbackChapterTitle, String bookTitle) :
            output_(output),
            wordCount_(wordCount),
            maxWords_(maxWords),
            lastChapterTitle_(lastChapterTitle),
            chapterCount_(chapterCount),
            tocEntries_(tocEntries),
            hasToc_(hasToc),
            fallbackChapterTitle_(std::move(fallbackChapterTitle)),
            bookTitle_(std::move(bookTitle)) {
        line_.reserve(160);
        heading_.reserve(80);
        tag_.reserve(96);
        entity_.reserve(16);

        if (tocEntries_.size() == 1) {
            writeChapter(tocEntries_.front().title);
            nextTocEntry_ = 1;
        }
    }

    bool RsvpContentWriter::write(const uint8_t* data, size_t length) {
        for (size_t i = 0; i < length; ++i) {
            if ((i & 0x3FF) == 0) {
                serviceBackground();
            }
            if (!processChar(static_cast<char>(data[i]))) {
                return false;
            }
        }

        return true;
    }

    bool RsvpContentWriter::finish() {
        mode_ = Mode::Text;
        if (!flushLine()) {
            return false;
        }
        return tocEntries_.empty() || emitTocEntriesThrough(tocEntries_.size() - 1);
    }

    bool RsvpContentWriter::reachedWordLimit() const {
        return reachedWordLimit_;
    }

    void RsvpContentWriter::beginParagraph() {
        if (paragraphOpen_) {
            return;
        }
        if (!documentChapterWritten_ && !hasToc_ && !fallbackChapterTitle_.isEmpty()) {
            writeChapter(fallbackChapterTitle_);
        }
        if (wordCount_ > 0) {
            output_.println();
            output_.println("@para");
        }
        paragraphOpen_ = true;
    }

    bool RsvpContentWriter::flushLine(bool endParagraph) {
        line_.trim();
        if (line_.isEmpty()) {
            if (endParagraph) {
                paragraphOpen_ = false;
            }
            return true;
        }

        beginParagraph();
        const bool keepGoing = writeBodyLine(output_, line_, wordCount_, maxWords_);
        line_ = "";
        if (endParagraph) {
            paragraphOpen_ = false;
        }
        if (!keepGoing) {
            reachedWordLimit_ = true;
        }
        return keepGoing;
    }

    bool RsvpContentWriter::flushWordAlignedPrefix() {
        line_.trim();
        int split = static_cast<int>(line_.length()) - 1;
        while (split >= 0 && !AsciiText::isWhitespace(line_[split])) {
            --split;
        }
        if (split < 0) {
            return true;
        }

        String prefix = line_.substring(0, split);
        String remainder = line_.substring(split + 1);
        prefix.trim();
        remainder.trim();
        if (prefix.isEmpty()) {
            line_ = remainder;
            return true;
        }

        line_ = prefix;
        const bool keepGoing = flushLine(false);
        line_ = remainder;
        return keepGoing;
    }

    bool RsvpContentWriter::writeChapter(const String& title) {
        String cleaned = RsvpText::normalizeDisplayText(title);
        cleaned.trim();
        if (cleaned.isEmpty() || cleaned == lastChapterTitle_) {
            return true;
        }
        if (wordCount_ > 0 || !lastChapterTitle_.isEmpty()) {
            output_.println();
        }
        output_.print("@chapter ");
        output_.println(cleaned);
        lastChapterTitle_ = cleaned;
        ++chapterCount_;
        documentChapterWritten_ = true;
        paragraphOpen_ = false;
        return true;
    }

    bool RsvpContentWriter::emitTocEntriesThrough(size_t index) {
        if (nextTocEntry_ > index || nextTocEntry_ >= tocEntries_.size()) {
            return true;
        }
        if (!flushLine()) {
            return false;
        }
        while (nextTocEntry_ <= index && nextTocEntry_ < tocEntries_.size()) {
            if (!writeChapter(tocEntries_[nextTocEntry_].title)) {
                return false;
            }
            ++nextTocEntry_;
        }
        return true;
    }

    int RsvpContentWriter::matchingTocEntry(const String& anchor) const {
        if (anchor.isEmpty()) {
            return -1;
        }
        for (size_t i = nextTocEntry_; i < tocEntries_.size(); ++i) {
            if (!tocEntries_[i].fragment.isEmpty() && tocEntries_[i].fragment == anchor) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    bool RsvpContentWriter::suppressHeading(const String& heading) const {
        if (!hasToc_) {
            return false;
        }
        const String normalized = normalizedLabel(heading);
        return normalized == "contents" || normalized == "tableofcontents"
            || (!bookTitle_.isEmpty() && normalized == normalizedLabel(bookTitle_));
    }

    void RsvpContentWriter::appendToActiveText(char c) {
        if (inHeading_) {
            appendNormalizedChar(heading_, c);
            return;
        }

        appendNormalizedChar(line_, c);
    }

    bool RsvpContentWriter::processDecodedText(char c) {
        if (skipDepth_ > 0) {
            return true;
        }

        appendToActiveText(c);
        if (!inHeading_ && line_.length() > kBufferedTextFlushThreshold) {
            return flushWordAlignedPrefix();
        }

        return true;
    }

    bool RsvpContentWriter::processTextChar(char c) {
        if (c == '<') {
            tag_ = "<";
            mode_ = Mode::Tag;
            return true;
        }

        if (c == '&') {
            if (skipDepth_ > 0) {
                return true;
            }
            entity_ = "";
            mode_ = Mode::Entity;
            return true;
        }

        return processDecodedText(c);
    }

    bool RsvpContentWriter::processTag(const String& tag) {
        const TagInfo tagInfo = parseTagInfo(tag);

        if (tagInfo.name.isEmpty() || tag.startsWith("<!") || tag.startsWith("<?")) {
            return true;
        }

        const bool skipTag = isSkipTag(tagInfo.name);

        // Skip ignored tag subtrees without letting their text reach the output
        // buffer.
        {
            if (skipDepth_ > 0) {
                if (!tagInfo.closing && skipTag && !tagInfo.selfClosing) {
                    ++skipDepth_;
                } else if (tagInfo.closing && skipTag) {
                    --skipDepth_;
                }
                return true;
            }

            if (skipTag && !tagInfo.closing && !tagInfo.selfClosing) {
                if (!flushLine()) {
                    return false;
                }
                skipDepth_ = 1;
                return true;
            }
        }

        const int tocEntry = tagInfo.closing ? -1 : matchingTocEntry(tagInfo.anchor);
        if (tocEntry >= 0 && !emitTocEntriesThrough(static_cast<size_t>(tocEntry))) {
            return false;
        }

        // Headings become RSVP chapter markers instead of body words.
        {
            if (isHeadingTag(tagInfo.name)) {
                if (tagInfo.closing) {
                    inHeading_ = false;
                    const String cleanedHeading = plainTextFromXmlFragment(heading_);
                    if (tocEntries_.empty() && !suppressHeading(cleanedHeading) && !writeChapter(cleanedHeading)) {
                        return false;
                    }
                    heading_ = "";
                } else if (!tagInfo.selfClosing) {
                    if (!flushLine()) {
                        return false;
                    }
                    if (!tocEntries_.empty() && tocEntry < 0 && nextTocEntry_ < tocEntries_.size()
                        && !emitTocEntriesThrough(nextTocEntry_)) {
                        return false;
                    }
                    inHeading_ = true;
                    heading_ = "";
                }
                return true;
            }
        }

        // Block tags either flush buffered words or introduce a spacing boundary.
        {
            const bool blockTag = isBlockTag(tagInfo.name);
            if (blockTag && (tagInfo.closing || tagInfo.name == "br" || tagInfo.name == "hr" || tagInfo.name == "li")) {
                return flushLine();
            }
            if (blockTag) {
                appendNormalizedChar(line_, ' ');
            }
        }

        return true;
    }

    bool RsvpContentWriter::processEntityChar(char c) {
        if (c == ';') {
            mode_ = Mode::Text;
            const String decoded = decodedEntityText(entity_);
            const bool processed =
                std::all_of(decoded.c_str(), decoded.c_str() + decoded.length(), [&](char decodedChar) {
                    return processDecodedText(decodedChar);
                });
            if (!processed) {
                return false;
            }
            return true;
        }

        if (c == '<') {
            mode_ = Mode::Text;
            if (!processDecodedText(' ')) {
                return false;
            }
            return processTextChar(c);
        }

        if (entity_.length() >= kMaxEntityChars || AsciiText::isWhitespace(c)) {
            mode_ = Mode::Text;
            return processDecodedText(' ');
        }

        entity_ += c;
        return true;
    }

    bool RsvpContentWriter::processCommentChar(char c) {
        commentTail_ += c;
        if (commentTail_.length() > 3) {
            commentTail_.remove(0, commentTail_.length() - 3);
        }

        if (commentTail_ == "-->") {
            commentTail_ = "";
            mode_ = Mode::Text;
        }

        return true;
    }

    bool RsvpContentWriter::processChar(char c) {
        switch (mode_) {
        case Mode::Text:
            return processTextChar(c);
        case Mode::Entity:
            return processEntityChar(c);
        case Mode::Comment:
            return processCommentChar(c);
        case Mode::Tag:
            tag_ += c;
            if (tag_ == "<!--") {
                tag_ = "";
                commentTail_ = "";
                mode_ = Mode::Comment;
                return true;
            }
            if (tag_.length() > kMaxTagChars) {
                tag_ = "";
                mode_ = Mode::Text;
                return processDecodedText(' ');
            }
            if (c == '>') {
                const String completedTag = tag_;
                tag_ = "";
                mode_ = Mode::Text;
                return processTag(completedTag);
            }
            return true;
        }

        return true;
    }

} // namespace EpubContent
