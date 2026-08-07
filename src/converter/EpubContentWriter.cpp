#include "converter/EpubContentWriter.h"

#include <algorithm>
#include <utility>

#include "text/LocaleTag.h"
#include "text/AsciiText.h"
#include "text/RsvpTokenizer.h"
#include "text/TextNormalizer.h"
#include "text/UnicodeText.h"

namespace EpubContent {
    namespace {

        constexpr size_t kMaxEntityChars = 16;
        constexpr size_t kMaxTagChars = 512;
        constexpr size_t kOutputWrapWidth = 96;
        constexpr size_t kBufferedTextFlushThreshold = 220;

        struct TagInfo {
            std::string name;
            std::string anchor;
            std::string locale;
            std::string direction;
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

        std::string decodedEntityText(std::string_view entity) {
            std::string decoded;
            return RsvpText::decodeMarkupEntity(entity, decoded) ? decoded : " ";
        }

        void appendNormalizedChar(std::string& target, char c) {
            if (AsciiText::isWhitespace(c)) {
                if (!target.empty() && target.back() != ' ') {
                    target += ' ';
                }
                return;
            }

            target += c;
        }

        std::string tagAttributeValue(std::string_view tag, std::string_view name) {
            size_t position = 0;
            while ((position = tag.find(name, position)) != std::string_view::npos) {
                const bool boundaryBefore = position == 0 || AsciiText::isWhitespace(tag[position - 1])
                                         || tag[position - 1] == '<' || tag[position - 1] == '/';
                size_t afterName = position + name.length();
                const bool boundaryAfter =
                    afterName >= tag.length() || AsciiText::isWhitespace(tag[afterName]) || tag[afterName] == '=';
                if (!boundaryBefore || !boundaryAfter) {
                    position = afterName;
                    continue;
                }
                while (afterName < tag.length() && AsciiText::isWhitespace(tag[afterName])) {
                    ++afterName;
                }
                if (afterName >= tag.length() || tag[afterName] != '=') {
                    position = afterName;
                    continue;
                }
                do {
                    ++afterName;
                } while (afterName < tag.length() && AsciiText::isWhitespace(tag[afterName]));
                if (afterName >= tag.length()) {
                    return {};
                }

                const char quote = tag[afterName];
                if (quote == '"' || quote == '\'') {
                    const size_t end = tag.find(quote, afterName + 1);
                    return end == std::string_view::npos ? std::string{}
                                                         : std::string{tag.substr(afterName + 1, end - afterName - 1)};
                }
                size_t end = afterName;
                while (end < tag.length() && !AsciiText::isWhitespace(tag[end]) && tag[end] != '>') {
                    ++end;
                }
                return std::string{tag.substr(afterName, end - afterName)};
            }
            return {};
        }

        TagInfo parseTagInfo(std::string_view tag) {
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

            info.name = tag.substr(start, position - start);
            std::ranges::transform(info.name, info.name.begin(), AsciiText::toLower);
            info.anchor = tagAttributeValue(tag, "id");
            if (info.anchor.empty()) {
                info.anchor = tagAttributeValue(tag, "name");
            }
            info.anchor = std::string{AsciiText::trim(info.anchor)};
            info.locale = tagAttributeValue(tag, "xml:lang");
            if (info.locale.empty())
                info.locale = tagAttributeValue(tag, "lang");
            if (!info.locale.empty()) {
                auto normalized = LocaleTag::normalize(AsciiText::trim(info.locale));
                info.locale = normalized ? std::move(*normalized) : std::string{};
            }
            info.direction = std::string{AsciiText::trim(tagAttributeValue(tag, "dir"))};
            std::ranges::transform(info.direction, info.direction.begin(), AsciiText::toLower);
            if (info.direction != "auto" && info.direction != "ltr" && info.direction != "rtl")
                info.direction.clear();

            for (int i = static_cast<int>(tag.length()) - 1; i >= 0; --i) {
                if (AsciiText::isWhitespace(tag[i]) || tag[i] == '>') {
                    continue;
                }
                info.selfClosing = tag[i] == '/';
                break;
            }

            return info;
        }

        bool isSkipTag(std::string_view name) {
            static constexpr std::string_view kSkippedTags[] = {
                "head", "script", "style", "svg", "math", "nav",
            };
            return std::ranges::find(kSkippedTags, name) != std::ranges::end(kSkippedTags);
        }

        bool isHeadingTag(std::string_view name) {
            return name.length() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6';
        }

        bool isBlockTag(std::string_view name) {
            static constexpr std::string_view kBlockTags[] = {
                "address",    "article", "aside",  "blockquote", "body",  "br", "dd",    "div", "dl", "dt",
                "figcaption", "figure",  "footer", "header",     "hr",    "li", "main",  "ol",  "p",  "pre",
                "section",    "table",   "tbody",  "td",         "tfoot", "th", "thead", "tr",  "ul",
            };
            return std::ranges::find(kBlockTags, name) != std::ranges::end(kBlockTags);
        }

        bool isVoidTag(std::string_view name) {
            static constexpr std::string_view kVoidTags[] = {
                "area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param",
                "source", "track", "wbr",
            };
            return std::ranges::find(kVoidTags, name) != std::ranges::end(kVoidTags);
        }

        std::string normalizedLabel(std::string_view value) {
            return RsvpText::readableKey(value);
        }

    } // namespace

    std::string plainTextFromXmlFragment(std::string_view fragment) {
        std::string text;
        text.reserve(std::min<size_t>(fragment.length(), 160));

        for (size_t i = 0; i < fragment.length(); ++i) {
            const char c = fragment[i];
            if (c == '<') {
                const size_t tagEnd = fragment.find('>', i + 1);
                if (tagEnd == std::string_view::npos) {
                    break;
                }
                i = tagEnd;
                appendNormalizedChar(text, ' ');
                continue;
            }

            if (c == '&') {
                const size_t entityEnd = fragment.find(';', i + 1);
                if (entityEnd != std::string_view::npos && entityEnd - i - 1 <= kMaxEntityChars) {
                    const std::string decoded = decodedEntityText(fragment.substr(i + 1, entityEnd - i - 1));
                    std::ranges::for_each(decoded, [&](char decodedChar) {
                        appendNormalizedChar(text, decodedChar);
                    });
                    i = entityEnd;
                    continue;
                }
            }

            appendNormalizedChar(text, c);
        }

        return RsvpText::normalizeDisplayText(text);
    }

    bool writeBodyLine(File& output, std::string_view line, size_t& wordCount, size_t maxWords) {
        const std::string normalizedLine = RsvpText::normalizeDisplayText(line);
        std::string outputLine;
        bool previousCjk = false;

        auto flushOutputLine = [&]() {
            if (outputLine.empty()) {
                return;
            }
            if (outputLine.starts_with('@')) {
                output.print('@');
            }
            output.println(outputLine.c_str());
            outputLine.clear();
            previousCjk = false;
        };

        auto consumeRsvpToken = [&](const std::string& value) {
            if (value.empty()) {
                return true;
            }

            const bool cjk = UnicodeText::isCjkText(value);
            const bool separated = !outputLine.empty() && !(previousCjk && cjk);
            if (outputLine.length() + value.length() + separated > kOutputWrapWidth) {
                flushOutputLine();
            }

            if (!outputLine.empty() && !(previousCjk && cjk)) {
                outputLine += ' ';
            }
            outputLine += value;
            previousCjk = cjk;
            if (RsvpText::hasReadableText(value)) {
                ++wordCount;
            }
            return true;
        };

        const bool keepGoing =
            RsvpText::appendNormalizedLineWords(normalizedLine, consumeRsvpToken, wordCount, maxWords);

        flushOutputLine();
        return keepGoing;
    }

    RsvpContentWriter::RsvpContentWriter(File& output, size_t& wordCount, size_t maxWords,
                                         std::string& lastChapterTitle, size_t& chapterCount,
                                         std::span<const EpubPackage::TocEntry> tocEntries, bool hasToc,
                                         std::string_view fallbackChapterTitle, std::string_view bookTitle,
                                         std::string_view initialLocale, std::string_view initialDirection) :
            output_(output),
            wordCount_(wordCount),
            maxWords_(maxWords),
            lastChapterTitle_(lastChapterTitle),
            chapterCount_(chapterCount),
            tocEntries_(tocEntries),
            hasToc_(hasToc),
            fallbackChapterTitle_(fallbackChapterTitle),
            bookTitle_(bookTitle),
            locale_(initialLocale.empty() ? "und" : initialLocale),
            direction_(initialDirection) {
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

    bool RsvpContentWriter::changeLanguageState(std::string_view locale, std::string_view direction) {
        if (locale == locale_ && direction == direction_)
            return true;
        if (!flushLine(false))
            return false;
        if (locale != locale_) {
            std::string next = locale.empty() ? "und" : std::string{locale};
            output_.print("@language ");
            output_.println(next.c_str());
            locale_ = std::move(next);
        }
        if (direction != direction_) {
            std::string next = direction.empty() ? "auto" : std::string{direction};
            output_.print("@direction ");
            output_.println(next.c_str());
            direction_ = std::move(next);
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
        if (!documentChapterWritten_ && !hasToc_ && !fallbackChapterTitle_.empty()) {
            writeChapter(fallbackChapterTitle_);
        }
        if (wordCount_ > 0) {
            output_.println();
            output_.println("@para");
        }
        paragraphOpen_ = true;
    }

    bool RsvpContentWriter::flushLine(bool endParagraph) {
        line_ = std::string{AsciiText::trim(line_)};
        if (line_.empty()) {
            if (endParagraph) {
                paragraphOpen_ = false;
            }
            return true;
        }

        beginParagraph();
        const bool keepGoing = writeBodyLine(output_, line_, wordCount_, maxWords_);
        line_.clear();
        if (endParagraph) {
            paragraphOpen_ = false;
        }
        if (!keepGoing) {
            reachedWordLimit_ = true;
        }
        return keepGoing;
    }

    bool RsvpContentWriter::flushWordAlignedPrefix() {
        line_ = std::string{AsciiText::trim(line_)};
        int split = static_cast<int>(line_.length()) - 1;
        while (split >= 0 && !AsciiText::isWhitespace(line_[split])) {
            --split;
        }
        if (split < 0) {
            return true;
        }

        std::string prefix{AsciiText::trim(std::string_view{line_}.substr(0, split))};
        std::string remainder{AsciiText::trim(std::string_view{line_}.substr(split + 1))};
        if (prefix.empty()) {
            line_ = remainder;
            return true;
        }

        line_ = prefix;
        const bool keepGoing = flushLine(false);
        line_ = remainder;
        return keepGoing;
    }

    bool RsvpContentWriter::writeChapter(std::string_view title) {
        const std::string cleaned{AsciiText::trim(RsvpText::normalizeDisplayText(title))};
        if (cleaned.empty() || cleaned == lastChapterTitle_) {
            return true;
        }
        if (wordCount_ > 0 || !lastChapterTitle_.empty()) {
            output_.println();
        }
        output_.print("@chapter ");
        output_.println(cleaned.c_str());
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

    int RsvpContentWriter::matchingTocEntry(std::string_view anchor) const {
        if (anchor.empty()) {
            return -1;
        }
        for (size_t i = nextTocEntry_; i < tocEntries_.size(); ++i) {
            if (!tocEntries_[i].fragment.empty() && tocEntries_[i].fragment == anchor) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    bool RsvpContentWriter::suppressHeading(std::string_view heading) const {
        if (!hasToc_) {
            return false;
        }
        const std::string normalized = normalizedLabel(heading);
        return normalized == "contents" || normalized == "tableofcontents"
            || (!bookTitle_.empty() && normalized == normalizedLabel(bookTitle_));
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
            entity_.clear();
            mode_ = Mode::Entity;
            return true;
        }

        return processDecodedText(c);
    }

    bool RsvpContentWriter::processTag(std::string_view tag) {
        const TagInfo tagInfo = parseTagInfo(tag);

        if (tagInfo.name.empty() || tag.starts_with("<!") || tag.starts_with("<?")) {
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

        if (!tagInfo.closing && !tagInfo.selfClosing && !isVoidTag(tagInfo.name)) {
            const std::string nextLocale = tagInfo.locale.empty() ? locale_ : tagInfo.locale;
            const std::string nextDirection = tagInfo.direction.empty() ? direction_ : tagInfo.direction;
            const bool changed = nextLocale != locale_ || nextDirection != direction_;
            languageScopes_.push_back({tagInfo.name, locale_, direction_, changed});
            if (changed) {
                if (!changeLanguageState(nextLocale, nextDirection))
                    return false;
            }
        } else if (tagInfo.closing && !languageScopes_.empty() && languageScopes_.back().tag == tagInfo.name) {
            LanguageScope scope = std::move(languageScopes_.back());
            languageScopes_.pop_back();
            if (scope.changed && !changeLanguageState(scope.locale, scope.direction))
                return false;
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
                    const std::string cleanedHeading = plainTextFromXmlFragment(heading_);
                    if (tocEntries_.empty() && !suppressHeading(cleanedHeading) && !writeChapter(cleanedHeading)) {
                        return false;
                    }
                    heading_.clear();
                } else if (!tagInfo.selfClosing) {
                    if (!flushLine()) {
                        return false;
                    }
                    if (!tocEntries_.empty() && tocEntry < 0 && nextTocEntry_ < tocEntries_.size()
                        && !emitTocEntriesThrough(nextTocEntry_)) {
                        return false;
                    }
                    inHeading_ = true;
                    heading_.clear();
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
            const std::string decoded = decodedEntityText(entity_);
            const bool processed = std::ranges::all_of(decoded, [&](char decodedChar) {
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
            commentTail_.erase(0, commentTail_.length() - 3);
        }

        if (commentTail_ == "-->") {
            commentTail_.clear();
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
                tag_.clear();
                commentTail_.clear();
                mode_ = Mode::Comment;
                return true;
            }
            if (tag_.length() > kMaxTagChars) {
                tag_.clear();
                mode_ = Mode::Text;
                return processDecodedText(' ');
            }
            if (c == '>') {
                const std::string completedTag = std::move(tag_);
                tag_.clear();
                mode_ = Mode::Text;
                return processTag(completedTag);
            }
            return true;
        }

        return true;
    }

} // namespace EpubContent
