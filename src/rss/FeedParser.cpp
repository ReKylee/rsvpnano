#include "rss/FeedParser.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "text/AsciiText.h"
#include "text/TextNormalizer.h"

namespace feedparser {
    namespace {

        bool matchesIgnoreCaseAt(const String& text, size_t index, const char* needle) {
            for (size_t i = 0; needle[i] != '\0'; ++i) {
                if (index + i >= text.length()
                    || AsciiText::toLower(text[index + i]) != AsciiText::toLower(needle[i])) {
                    return false;
                }
            }
            return true;
        }

        int indexOfIgnoreCase(const String& text, const char* needle, size_t start, size_t limit) {
            const size_t needleLength = strlen(needle);
            if (needleLength == 0 || start >= text.length()) {
                return -1;
            }
            limit = std::min(limit, static_cast<size_t>(text.length()));
            if (limit < needleLength) {
                return -1;
            }
            for (size_t i = start; i + needleLength <= limit; ++i) {
                if (matchesIgnoreCaseAt(text, i, needle)) {
                    return static_cast<int>(i);
                }
            }
            return -1;
        }

        int tagEndIndex(const String& text, size_t start, size_t limit) {
            limit = std::min(limit, static_cast<size_t>(text.length()));
            for (size_t i = start; i < limit; ++i) {
                if (text[i] == '>') {
                    return static_cast<int>(i);
                }
            }
            return -1;
        }

        struct ItemBounds {
            size_t start;
            size_t end;
            size_t next;
        };

        bool findNextItem(const String& feedBody, size_t searchStart, ItemBounds& bounds) {
            int itemStart = indexOfIgnoreCase(feedBody, "<item", searchStart, feedBody.length());
            bool atom = false;
            if (itemStart < 0) {
                itemStart = indexOfIgnoreCase(feedBody, "<entry", searchStart, feedBody.length());
                atom = itemStart >= 0;
            }
            if (itemStart < 0) {
                return false;
            }

            const char* closeTag = atom ? "</entry>" : "</item>";
            const int itemEnd = indexOfIgnoreCase(feedBody, closeTag, itemStart, feedBody.length());
            if (itemEnd < 0) {
                return false;
            }

            bounds = {
                .start = static_cast<size_t>(itemStart),
                .end = static_cast<size_t>(itemEnd),
                .next = static_cast<size_t>(itemEnd) + strlen(closeTag),
            };
            return true;
        }

        String valueBetween(const String& text, const String& openTag, const String& closeTag, size_t start,
                            size_t end) {
            const int open = indexOfIgnoreCase(text, openTag.c_str(), start, end);
            if (open < 0 || static_cast<size_t>(open) >= end) {
                return "";
            }
            const int valueStart = tagEndIndex(text, static_cast<size_t>(open), end);
            if (valueStart < 0 || static_cast<size_t>(valueStart) >= end) {
                return "";
            }
            const int close = indexOfIgnoreCase(text, closeTag.c_str(), valueStart + 1, end);
            if (close < 0 || static_cast<size_t>(close) > end) {
                return "";
            }
            return text.substring(valueStart + 1, close);
        }

        String attributeValue(const String& text, const String& tagPrefix, const String& attribute, size_t start,
                              size_t end) {
            int tagStart = indexOfIgnoreCase(text, tagPrefix.c_str(), start, end);
            while (tagStart >= 0 && static_cast<size_t>(tagStart) < end) {
                const int tagEnd = tagEndIndex(text, static_cast<size_t>(tagStart), end);
                if (tagEnd < 0 || static_cast<size_t>(tagEnd) > end) {
                    return "";
                }

                const String needle = attribute + "=";
                const int attrIndex = indexOfIgnoreCase(text, needle.c_str(), tagStart, static_cast<size_t>(tagEnd));
                if (attrIndex >= 0) {
                    int valueStart = attrIndex + needle.length();
                    while (valueStart < tagEnd && isspace(static_cast<unsigned char>(text[valueStart]))) {
                        ++valueStart;
                    }
                    if (valueStart < tagEnd) {
                        const char quote = text[valueStart];
                        if (quote == '"' || quote == '\'') {
                            ++valueStart;
                            for (int i = valueStart; i < tagEnd; ++i) {
                                if (text[i] == quote) {
                                    return text.substring(valueStart, i);
                                }
                            }
                        } else {
                            int valueEnd = valueStart;
                            while (valueEnd < tagEnd && !isspace(static_cast<unsigned char>(text[valueEnd]))
                                   && text[valueEnd] != '>') {
                                ++valueEnd;
                            }
                            if (valueEnd > valueStart) {
                                return text.substring(valueStart, valueEnd);
                            }
                        }
                    }
                }

                tagStart = indexOfIgnoreCase(text, tagPrefix.c_str(), static_cast<size_t>(tagEnd + 1), end);
            }
            return "";
        }

        String stripHtml(const String& html) {
            String output;
            output.reserve(std::min(static_cast<size_t>(html.length()), kMaxArticleChars));
            bool inTag = false;
            for (size_t i = 0; i < html.length(); ++i) {
                const char c = html[i];
                if (c == '<') {
                    inTag = true;
                    if (!output.endsWith(" ") && !output.endsWith("\n")) {
                        output += ' ';
                    }
                    continue;
                }
                if (c == '>') {
                    inTag = false;
                    continue;
                }
                if (!inTag) {
                    output += c;
                }
                if (output.length() >= kMaxArticleChars) {
                    break;
                }
            }
            return output;
        }

        String cleanText(String value) {
            value.replace("<![CDATA[", "");
            value.replace("]]>", "");
            value = stripHtml(value);
            value = RsvpText::decodeMarkupEntities(value);
            if (value.indexOf('&') >= 0) {
                value = RsvpText::decodeMarkupEntities(value);
            }
            value.replace("\r", "\n");
            while (value.indexOf("\n\n\n") >= 0) {
                value.replace("\n\n\n", "\n\n");
            }
            value.trim();
            return value;
        }

    } // namespace

    bool hasCompleteFeed(const String& feedBody, size_t searchStart) {
        return indexOfIgnoreCase(feedBody, "</rss>", searchStart, feedBody.length()) >= 0
            || indexOfIgnoreCase(feedBody, "</feed>", searchStart, feedBody.length()) >= 0;
    }

    bool advancePastItem(const String& feedBody, size_t& searchStart) {
        ItemBounds bounds{};
        if (!findNextItem(feedBody, searchStart, bounds)) {
            return false;
        }
        searchStart = bounds.next;
        return true;
    }

    String hostLabelForUrl(const String& url) {
        int start = url.indexOf("://");
        start = start < 0 ? 0 : start + 3;
        int end = url.indexOf('/', start);
        if (end < 0) {
            end = url.length();
        }
        String host = url.substring(start, end);
        if (host.startsWith("www.")) {
            host.remove(0, 4);
        }
        return host;
    }

    String sourceLabelForItem(const FeedItem& item) {
        if (item.link.isEmpty()) {
            return "RSS";
        }

        const String source = hostLabelForUrl(item.link);
        return source.isEmpty() ? "RSS" : source;
    }

    bool parseNextItem(const String& feedBody, size_t& searchStart, FeedItem& item) {
        ItemBounds bounds{};
        if (!findNextItem(feedBody, searchStart, bounds)) {
            return false;
        }
        searchStart = bounds.next;

        item.title = cleanText(valueBetween(feedBody, "<title", "</title>", bounds.start, bounds.end));
        item.link = cleanText(valueBetween(feedBody, "<link>", "</link>", bounds.start, bounds.end));
        if (item.link.isEmpty()) {
            item.link = cleanText(attributeValue(feedBody, "<link", "href", bounds.start, bounds.end));
        }
        if (item.link.isEmpty()) {
            item.link = cleanText(valueBetween(feedBody, "<guid", "</guid>", bounds.start, bounds.end));
        }
        item.author = cleanText(valueBetween(feedBody, "<author", "</author>", bounds.start, bounds.end));
        if (item.author.isEmpty()) {
            item.author = cleanText(valueBetween(feedBody, "<dc:creator", "</dc:creator>", bounds.start, bounds.end));
        }
        if (item.author.isEmpty()) {
            item.author = sourceLabelForItem(item);
        }

        item.body =
            cleanText(valueBetween(feedBody, "<content:encoded", "</content:encoded>", bounds.start, bounds.end));
        if (item.body.isEmpty()) {
            item.body = cleanText(valueBetween(feedBody, "<content", "</content>", bounds.start, bounds.end));
        }
        if (item.body.isEmpty()) {
            item.body = cleanText(valueBetween(feedBody, "<description", "</description>", bounds.start, bounds.end));
        }
        if (item.body.isEmpty()) {
            item.body = cleanText(valueBetween(feedBody, "<summary", "</summary>", bounds.start, bounds.end));
        }
        if (item.body.isEmpty()) {
            item.body = item.link;
        }

        if (item.title.isEmpty()) {
            item.title = item.link.isEmpty() ? "RSS Article" : item.link;
        }
        return !item.body.isEmpty();
    }

} // namespace feedparser
