#include "conversion/epub/EpubPackage.h"

#include <algorithm>
#include <array>

#include "conversion/epub/EpubContentParser.h"
#include "text/AsciiText.h"
#include "text/RsvpTokenizer.h"

namespace EpubPackage {
    namespace {

        constexpr bool isAttributeNameBoundary(char c) {
            return AsciiText::isWhitespace(c) || c == '<' || c == '/';
        }

        constexpr bool isTagNameBoundary(char c) {
            return AsciiText::isWhitespace(c) || c == '/' || c == '>';
        }

        size_t skipAsciiWhitespace(std::string_view text, size_t position) {
            while (position < text.length() && AsciiText::isWhitespace(text[position])) {
                ++position;
            }
            return position;
        }

        std::string percentDecodePath(std::string_view path) {
            std::string decoded;
            decoded.reserve(path.length());

            for (size_t i = 0; i < path.length(); ++i) {
                if (path[i] == '%' && i + 2 < path.length()) {
                    if (auto byte = AsciiText::parseUnsigned<uint8_t>(path.substr(i + 1, 2), 16)) {
                        decoded += static_cast<char>(*byte);
                        i += 2;
                        continue;
                    }
                }
                decoded += path[i];
            }

            return decoded;
        }

        std::string normalizedTocLabel(std::string_view value) {
            return RsvpText::readableKey(EpubContent::plainTextFromXmlFragment(value));
        }

        bool isContentTocTitle(std::string_view value, std::string_view bookTitle) {
            const std::string cleaned = EpubContent::plainTextFromXmlFragment(value);
            const std::string lowered = toLowerCopy(cleaned);
            const std::string normalized = normalizedTocLabel(cleaned);
            const std::string normalizedBookTitle = normalizedTocLabel(bookTitle);
            return !cleaned.empty() && lowered != "contents" && lowered != "cover" && lowered != "title page"
                && normalized != "tableofcontents" && !normalized.empty()
                && (normalizedBookTitle.empty() || normalized != normalizedBookTitle)
                && (normalizedBookTitle.empty() || !normalizedBookTitle.starts_with(normalized));
        }

        std::string collapseZipPath(std::string_view path) {
            std::vector<std::string_view> parts;
            size_t start = 0;

            while (start <= path.length()) {
                size_t separator = path.find('/', start);
                if (separator == std::string_view::npos) {
                    separator = path.length();
                }

                const std::string_view part = path.substr(start, separator - start);
                if (part == "..") {
                    if (!parts.empty()) {
                        parts.pop_back();
                    }
                } else if (!part.empty() && part != ".") {
                    parts.push_back(part);
                }

                if (static_cast<size_t>(separator) >= path.length()) {
                    break;
                }
                start = static_cast<size_t>(separator) + 1;
            }

            std::string collapsed;
            collapsed.reserve(path.length());
            for (size_t i = 0; i < parts.size(); ++i) {
                if (i > 0) {
                    collapsed += '/';
                }
                collapsed += parts[i];
            }
            return collapsed;
        }

        std::string resolveZipPath(std::string_view baseDirectory, std::string_view href) {
            std::string path{href};

            size_t fragment = path.find('#');
            if (fragment != std::string::npos) {
                path.resize(fragment);
            }
            size_t query = path.find('?');
            if (query != std::string::npos) {
                path.resize(query);
            }

            path = percentDecodePath(path);
            path = normalizeZipName(path);
            if (!href.starts_with('/')) {
                path = std::string{baseDirectory} + path;
            }

            return collapseZipPath(path);
        }

        TocEntry tocEntry(std::string_view tocPath, std::string_view href, std::string_view title) {
            const size_t fragmentStart = href.find('#');
            std::string fragment =
                fragmentStart == std::string_view::npos ? std::string{} : std::string{href.substr(fragmentStart + 1)};
            const size_t queryStart = fragment.find('?');
            if (queryStart != std::string::npos) {
                fragment.resize(queryStart);
            }
            fragment = percentDecodePath(fragment);
            fragment = std::string{AsciiText::trim(fragment)};

            return {
                resolveZipPath(directoryForPath(tocPath), href),
                EpubContent::plainTextFromXmlFragment(title),
                fragment,
            };
        }

        std::string attributeValue(std::string_view tag, std::string_view name) {
            size_t position = 0;

            const auto readAttributeValue = [&](size_t valueStart) {
                const char quote = tag[valueStart];
                if (quote == '"' || quote == '\'') {
                    const size_t end = tag.find(quote, valueStart + 1);
                    return end == std::string_view::npos
                             ? std::string{}
                             : std::string{tag.substr(valueStart + 1, end - valueStart - 1)};
                }

                size_t end = valueStart;
                while (end < tag.length() && !AsciiText::isWhitespace(tag[end]) && tag[end] != '>') {
                    ++end;
                }
                return std::string{tag.substr(valueStart, end - valueStart)};
            };

            while (position < tag.length()) {
                position = tag.find(name, position);
                if (position == std::string_view::npos) {
                    return {};
                }

                const bool boundaryBefore = position == 0 || isAttributeNameBoundary(tag[position - 1]);
                size_t afterName = position + name.length();
                const bool boundaryAfter =
                    afterName >= tag.length() || AsciiText::isWhitespace(tag[afterName]) || tag[afterName] == '=';
                if (!boundaryBefore || !boundaryAfter) {
                    position = afterName;
                    continue;
                }

                afterName = skipAsciiWhitespace(tag, afterName);
                if (afterName >= tag.length() || tag[afterName] != '=') {
                    position = afterName;
                    continue;
                }
                afterName = skipAsciiWhitespace(tag, afterName + 1);
                if (afterName >= tag.length()) {
                    return {};
                }

                return readAttributeValue(afterName);
            }

            return {};
        }

        bool attributeContainsToken(std::string_view tag, std::string_view attribute, std::string_view wanted) {
            const std::string value = attributeValue(tag, attribute);
            size_t position = 0;
            while (position < value.length()) {
                while (position < value.length() && AsciiText::isWhitespace(value[position])) {
                    ++position;
                }
                size_t end = position;
                while (end < value.length() && !AsciiText::isWhitespace(value[end])) {
                    ++end;
                }
                if (std::string_view{value}.substr(position, end - position) == wanted) {
                    return true;
                }
                position = end;
            }
            return false;
        }

    } // namespace

    std::string toLowerCopy(std::string_view value) {
        std::string lowered{value};
        std::ranges::transform(lowered, lowered.begin(), AsciiText::toLower);
        return lowered;
    }

    std::string basenameWithoutExtension(std::string_view path) {
        const size_t separator = path.find_last_of('/');
        std::string_view name = separator == std::string_view::npos ? path : path.substr(separator + 1);
        const size_t dot = name.find_last_of('.');
        if (dot != std::string_view::npos && dot > 0) {
            name = name.substr(0, dot);
        }
        name = AsciiText::trim(name);
        return name.empty() ? "Untitled" : std::string{name};
    }

    std::string normalizeZipName(std::string_view path) {
        while (path.starts_with('/')) {
            path.remove_prefix(1);
        }
        std::string normalized{path};
        std::ranges::replace(normalized, '\\', '/');
        return normalized;
    }

    bool isArchiveHintEntry(std::string_view name) {
        static constexpr std::array<const char*, 5> kArchiveHintExtensions = {{
            ".opf",
            ".ncx",
            ".xhtml",
            ".html",
            ".htm",
        }};

        const std::string lowered = toLowerCopy(name);
        return lowered.contains("container") || std::ranges::any_of(kArchiveHintExtensions, [&](const char* extension) {
                   return lowered.ends_with(extension);
               });
    }

    std::string directoryForPath(std::string_view path) {
        const size_t separator = path.find_last_of('/');
        if (separator == std::string_view::npos) {
            return {};
        }
        return std::string{path.substr(0, separator + 1)};
    }

    bool isContentDocument(const ManifestItem& item) {
        static constexpr std::array<const char*, 3> kContentExtensions = {{
            ".xhtml",
            ".html",
            ".htm",
        }};

        const std::string mediaType = toLowerCopy(item.mediaType);
        const std::string path = toLowerCopy(item.path);
        return mediaType == "application/xhtml+xml" || mediaType == "text/html"
            || std::ranges::any_of(kContentExtensions, [&](const char* extension) {
                   return path.ends_with(extension);
               });
    }

    std::string parseRootfilePath(std::string_view containerXml) {
        size_t position = 0;
        while (position < containerXml.size()) {
            position = containerXml.find("<rootfile", position);
            if (position == std::string_view::npos) {
                break;
            }

            const size_t end = containerXml.find('>', position);
            if (end == std::string_view::npos) {
                break;
            }

            const std::string_view tag = containerXml.substr(position, end - position + 1);
            const std::string path = attributeValue(tag, "full-path");
            if (!path.empty()) {
                return normalizeZipName(path);
            }

            position = end + 1;
        }

        return {};
    }

    std::string parseDcMetadata(std::string_view opfXml, std::string_view tagName) {
        const std::string openTag = std::string("<dc:") + std::string{tagName};
        const std::string closeTag = std::string("</dc:") + std::string{tagName};
        size_t position = 0;
        while (position < opfXml.size()) {
            position = opfXml.find(openTag, position);
            if (position == std::string_view::npos) {
                break;
            }

            const size_t openEnd = opfXml.find('>', position);
            if (openEnd == std::string_view::npos) {
                break;
            }
            const size_t closeStart = opfXml.find(closeTag, openEnd + 1);
            if (closeStart == std::string_view::npos) {
                break;
            }

            const std::string value =
                EpubContent::plainTextFromXmlFragment(opfXml.substr(openEnd + 1, closeStart - openEnd - 1));
            if (!value.empty()) {
                return value;
            }

            position = closeStart + 1;
        }

        return {};
    }

    std::string parsePackageVersion(std::string_view opfXml) {
        const size_t position = opfXml.find("<package");
        if (position == std::string_view::npos) {
            return {};
        }

        const size_t end = opfXml.find('>', position);
        return end == std::string_view::npos ? std::string{}
                                             : attributeValue(opfXml.substr(position, end - position + 1), "version");
    }

    std::vector<ManifestItem> parseManifestItems(std::string_view opfXml, std::string_view opfBaseDir) {
        std::vector<ManifestItem> items;
        size_t position = 0;

        while (position < opfXml.size()) {
            position = opfXml.find("<item", position);
            if (position == std::string_view::npos) {
                break;
            }

            const size_t afterName = position + 5;
            if (afterName < opfXml.length() && !isTagNameBoundary(opfXml[afterName])) {
                position = afterName;
                continue;
            }

            const size_t end = opfXml.find('>', position);
            if (end == std::string_view::npos) {
                break;
            }

            const std::string_view tag = opfXml.substr(position, end - position + 1);
            ManifestItem item;
            item.id = attributeValue(tag, "id");
            item.path = resolveZipPath(opfBaseDir, attributeValue(tag, "href"));
            item.mediaType = attributeValue(tag, "media-type");
            item.properties = attributeValue(tag, "properties");

            if (!item.id.empty() && !item.path.empty()) {
                items.push_back(item);
            }

            position = end + 1;
        }

        return items;
    }

    std::vector<std::string> parseSpineIds(std::string_view opfXml) {
        std::vector<std::string> ids;
        size_t position = 0;

        while (position < opfXml.size()) {
            position = opfXml.find("<itemref", position);
            if (position == std::string_view::npos) {
                break;
            }

            const size_t end = opfXml.find('>', position);
            if (end == std::string_view::npos) {
                break;
            }

            const std::string_view tag = opfXml.substr(position, end - position + 1);
            const std::string idref = attributeValue(tag, "idref");
            if (!idref.empty()) {
                ids.push_back(idref);
            }

            position = end + 1;
        }

        return ids;
    }

    std::vector<TocEntry> parseNcxTocEntries(std::string_view xml, std::string_view tocPath,
                                             std::string_view bookTitle) {
        std::vector<TocEntry> entries;
        const std::string lowered = toLowerCopy(xml);
        size_t position = 0;

        while ((position = lowered.find("<navpoint", position)) != std::string::npos) {
            const size_t next = lowered.find("<navpoint", position + 9);
            const size_t close = lowered.find("</navpoint", position + 9);
            const size_t sectionEnd =
                next != std::string::npos && (close == std::string::npos || next < close) ? next : close;
            if (sectionEnd == std::string::npos) {
                break;
            }

            const size_t textStart = lowered.find("<text", position);
            const size_t contentStart = lowered.find("<content", position);
            if (textStart != std::string::npos && textStart < sectionEnd && contentStart != std::string::npos
                && contentStart < sectionEnd) {
                const size_t textOpenEnd = lowered.find('>', textStart);
                const size_t textClose = lowered.find("</text", textOpenEnd + 1);
                const size_t contentEnd = lowered.find('>', contentStart);
                if (textOpenEnd != std::string::npos && textClose != std::string::npos && textClose < sectionEnd
                    && contentEnd != std::string::npos) {
                    const std::string_view title = xml.substr(textOpenEnd + 1, textClose - textOpenEnd - 1);
                    const std::string href =
                        attributeValue(xml.substr(contentStart, contentEnd - contentStart + 1), "src");
                    if (!href.empty() && isContentTocTitle(title, bookTitle)) {
                        entries.push_back(tocEntry(tocPath, href, title));
                    }
                }
            }

            position += 9;
        }

        return entries;
    }

    std::vector<TocEntry> parseNavTocEntries(std::string_view markup, std::string_view tocPath,
                                             std::string_view bookTitle) {
        std::vector<TocEntry> entries;
        const std::string lowered = toLowerCopy(markup);
        size_t scanStart = 0;
        size_t scanEnd = markup.length();
        size_t navPosition = 0;

        while ((navPosition = lowered.find("<nav", navPosition)) != std::string::npos) {
            const size_t openEnd = lowered.find('>', navPosition);
            if (openEnd == std::string::npos) {
                break;
            }
            const std::string_view navTag{lowered.data() + navPosition, openEnd - navPosition + 1};
            if (attributeContainsToken(navTag, "epub:type", "toc") || attributeContainsToken(navTag, "type", "toc")) {
                scanStart = openEnd + 1;
                const size_t close = lowered.find("</nav", scanStart);
                scanEnd = close == std::string::npos ? markup.length() : close;
                break;
            }
            navPosition = openEnd + 1;
        }

        size_t position = scanStart;

        while ((position = lowered.find("<a", position)) != std::string::npos && position < scanEnd) {
            const size_t afterName = position + 2;
            if (afterName < markup.length() && !isTagNameBoundary(markup[afterName])) {
                position = afterName;
                continue;
            }

            const size_t openEnd = lowered.find('>', position);
            const size_t close = openEnd == std::string::npos ? std::string::npos : lowered.find("</a", openEnd + 1);
            if (openEnd == std::string::npos || close == std::string::npos || close > scanEnd) {
                break;
            }

            const std::string href = attributeValue(markup.substr(position, openEnd - position + 1), "href");
            const std::string_view title = markup.substr(openEnd + 1, close - openEnd - 1);
            if (!href.empty() && isContentTocTitle(title, bookTitle)) {
                entries.push_back(tocEntry(tocPath, href, title));
            }
            position = close + 3;
        }

        return entries;
    }

    const ManifestItem* findManifestItem(const std::vector<ManifestItem>& items, std::string_view id) {
        const auto item = std::ranges::find(items, id, &ManifestItem::id);
        return item == items.end() ? nullptr : &(*item);
    }

} // namespace EpubPackage
