#include "conversion/pdf/PdfTextExtractor.h"

#include <esp_log.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "text/AsciiText.h"

namespace {

    constexpr size_t kTokenBytes = 4096;
    constexpr size_t kMaximumCMapEntries = 16384;
    constexpr size_t kMaximumCMapTextBytes = 256 * 1024;
    constexpr size_t kMaximumPageCharacterMaps = 64;
    constexpr size_t kMaximumMappingBytes = 32;
    constexpr size_t kMaximumBufferedLineBytes = 1024;

    struct CMapEntry {
        uint32_t source = 0;
        uint32_t textOffset = 0;
        uint8_t textBytes = 0;
    };

    struct CMapBudget {
        size_t entries = 0;
        size_t textBytes = 0;
    };

    struct CharacterMap {
        std::array<uint32_t, 256> simple{};
        std::vector<CMapEntry> unicode;
        std::vector<char> text;
        size_t codeBytes = 1;
        bool composite = false;

        bool add(uint32_t source, std::string_view value, CMapBudget& budget) {
            if (value.empty() || value.size() > kMaximumMappingBytes || budget.entries >= kMaximumCMapEntries
                || budget.textBytes + value.size() > kMaximumCMapTextBytes)
                return false;
            unicode.push_back({.source = source,
                               .textOffset = static_cast<uint32_t>(text.size()),
                               .textBytes = static_cast<uint8_t>(value.size())});
            text.insert(text.end(), value.begin(), value.end());
            ++budget.entries;
            budget.textBytes += value.size();
            return true;
        }

        void finish() {
            std::ranges::sort(unicode, {}, [](const CMapEntry& entry) {
                return std::pair{entry.source, entry.textOffset};
            });
            const auto duplicate = std::ranges::unique(unicode, {}, &CMapEntry::source);
            unicode.erase(duplicate.begin(), duplicate.end());
        }

        std::string_view mappedText(uint32_t source) const {
            const auto entry = std::ranges::lower_bound(unicode, source, {}, &CMapEntry::source);
            if (entry == unicode.end() || entry->source != source)
                return {};
            return {text.data() + entry->textOffset, entry->textBytes};
        }
    };

    bool logPdfError(pdfio_file_t*, const char* message, void*) {
        ESP_LOGW("pdf", "%s", message == nullptr ? "PDF error" : message);
        return true;
    }

    bool appendUtf8(std::string& output, uint32_t codepoint) {
        if (codepoint == 0 || codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF))
            return false;
        if (codepoint < 0x80) {
            output += static_cast<char>(codepoint);
        } else if (codepoint < 0x800) {
            output += static_cast<char>(0xC0 | (codepoint >> 6));
            output += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else if (codepoint < 0x10000) {
            output += static_cast<char>(0xE0 | (codepoint >> 12));
            output += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            output += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else {
            output += static_cast<char>(0xF0 | (codepoint >> 18));
            output += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
            output += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            output += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
        return true;
    }

    std::string_view hexPayload(std::string_view token) {
        if (!token.empty() && token.front() == '<')
            token.remove_prefix(1);
        if (!token.empty() && token.back() == '>')
            token.remove_suffix(1);
        return token;
    }

    bool nextHexByte(std::string_view payload, size_t& position, uint8_t& value) {
        if (position >= payload.size())
            return false;
        const int high = AsciiText::hexDigit(payload[position++]);
        const int low = position < payload.size() ? AsciiText::hexDigit(payload[position++]) : 0;
        if (high < 0 || low < 0)
            return false;
        value = static_cast<uint8_t>((high << 4) | low);
        return true;
    }

    bool parseSourceCode(std::string_view token, uint32_t& code, size_t& byteCount) {
        const std::string_view payload = hexPayload(token);
        byteCount = (payload.size() + 1) / 2;
        if (byteCount == 0 || byteCount > sizeof(code))
            return false;
        code = 0;
        size_t position = 0;
        uint8_t byte = 0;
        while (position < payload.size()) {
            if (!nextHexByte(payload, position, byte))
                return false;
            code = (code << 8) | byte;
        }
        return true;
    }

    bool decodeUtf16Hex(std::string_view token, std::string& output) {
        output.clear();
        const std::string_view payload = hexPayload(token);
        size_t position = 0;
        while (position < payload.size()) {
            uint8_t highByte = 0;
            uint8_t lowByte = 0;
            if (!nextHexByte(payload, position, highByte) || !nextHexByte(payload, position, lowByte))
                return false;
            uint32_t codepoint = static_cast<uint32_t>(highByte << 8) | lowByte;
            if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                uint8_t lowHigh = 0;
                uint8_t lowLow = 0;
                if (!nextHexByte(payload, position, lowHigh) || !nextHexByte(payload, position, lowLow))
                    return false;
                const uint32_t low = static_cast<uint32_t>(lowHigh << 8) | lowLow;
                if (low < 0xDC00 || low > 0xDFFF)
                    return false;
                codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
            }
            if (!appendUtf8(output, codepoint) || output.size() > kMaximumMappingBytes)
                return false;
        }
        return !output.empty();
    }

    bool decodeSingleUtf16Codepoint(std::string_view token, uint32_t& codepoint) {
        const std::string_view payload = hexPayload(token);
        size_t position = 0;
        uint8_t highByte = 0;
        uint8_t lowByte = 0;
        if (!nextHexByte(payload, position, highByte) || !nextHexByte(payload, position, lowByte))
            return false;
        codepoint = static_cast<uint32_t>(highByte << 8) | lowByte;
        if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
            uint8_t lowHigh = 0;
            uint8_t lowLow = 0;
            if (!nextHexByte(payload, position, lowHigh) || !nextHexByte(payload, position, lowLow))
                return false;
            const uint32_t low = static_cast<uint32_t>(lowHigh << 8) | lowLow;
            if (low < 0xDC00 || low > 0xDFFF)
                return false;
            codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
        }
        return position == payload.size();
    }

    uint32_t sourceCode(std::span<const uint8_t> bytes) {
        if (bytes.empty() || bytes.size() > sizeof(uint32_t))
            return 0;
        uint32_t code = 0;
        for (const uint8_t byte: bytes)
            code = (code << 8) | byte;
        return code;
    }

    pdfio_dict_t* pageResources(pdfio_obj_t* page) {
        for (pdfio_obj_t* current = page; current != nullptr;) {
            pdfio_dict_t* dict = pdfioObjGetDict(current);
            if (dict == nullptr)
                return nullptr;
            if (pdfio_dict_t* resources = pdfioDictGetDict(dict, "Resources"))
                return resources;
            current = pdfioDictGetObj(dict, "Parent");
        }
        return nullptr;
    }

    pdfio_dict_t* characterResources(pdfio_obj_t* page) {
        pdfio_dict_t* resources = pageResources(page);
        if (resources == nullptr)
            return nullptr;
        pdfio_dict_t* fonts = pdfioDictGetDict(resources, "Font");
        if (fonts == nullptr) {
            if (pdfio_obj_t* fontsObject = pdfioDictGetObj(resources, "Font"))
                fonts = pdfioObjGetDict(fontsObject);
        }
        return fonts;
    }

    pdfio_dict_t* characterResource(pdfio_obj_t* page, std::string_view name) {
        pdfio_dict_t* fonts = characterResources(page);
        if (fonts == nullptr)
            return nullptr;
        const std::string ownedName{name};
        if (pdfio_dict_t* font = pdfioDictGetDict(fonts, ownedName.c_str()))
            return font;
        if (pdfio_obj_t* fontObject = pdfioDictGetObj(fonts, ownedName.c_str()))
            return pdfioObjGetDict(fontObject);
        return nullptr;
    }

    uint32_t glyphNameCodepoint(std::string_view name) {
        if (name.size() == 1)
            return static_cast<uint8_t>(name.front());
        struct Glyph {
            std::string_view name;
            uint32_t codepoint;
        };
        static constexpr Glyph kCommonGlyphs[] = {
            {"space", 0x20}, {"hyphen", 0x2D}, {"quotesingle", 0x27}, {"quotedbl", 0x22},
            {"endash", 0x2013}, {"emdash", 0x2014}, {"ellipsis", 0x2026}, {"bullet", 0x2022},
            {"quoteleft", 0x2018}, {"quoteright", 0x2019}, {"quotedblleft", 0x201C},
            {"quotedblright", 0x201D}, {"Euro", 0x20AC}, {"fi", 0xFB01}, {"fl", 0xFB02},
        };
        if (const auto glyph = std::ranges::find(kCommonGlyphs, name, &Glyph::name); glyph != std::end(kCommonGlyphs))
            return glyph->codepoint;
        const bool longForm = name.starts_with("uni") && name.size() == 7;
        const bool shortForm = name.starts_with('u') && name.size() >= 5 && name.size() <= 7;
        if (!longForm && !shortForm)
            return 0;
        name.remove_prefix(longForm ? 3 : 1);
        uint32_t codepoint = 0;
        const auto [end, error] = std::from_chars(name.data(), name.data() + name.size(), codepoint, 16);
        return error == std::errc{} && end == name.data() + name.size() ? codepoint : 0;
    }

    void initializeWinAnsi(CharacterMap& map) {
        for (size_t i = 0; i < map.simple.size(); ++i)
            map.simple[i] = i;
        static constexpr std::array<uint32_t, 32> kWinAnsi = {
            0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
            0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
            0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
            0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178,
        };
        std::ranges::copy(kWinAnsi, map.simple.begin() + 128);
    }

    void applyDifferences(pdfio_dict_t* resource, CharacterMap& map) {
        pdfio_dict_t* encoding = pdfioDictGetDict(resource, "Encoding");
        if (encoding == nullptr) {
            if (pdfio_obj_t* object = pdfioDictGetObj(resource, "Encoding"))
                encoding = pdfioObjGetDict(object);
        }
        if (encoding == nullptr)
            return;
        pdfio_array_t* differences = pdfioDictGetArray(encoding, "Differences");
        if (differences == nullptr)
            return;
        size_t index = 0;
        for (size_t i = 0; i < pdfioArrayGetSize(differences); ++i) {
            if (pdfioArrayGetType(differences, i) == PDFIO_VALTYPE_NUMBER) {
                const double value = pdfioArrayGetNumber(differences, i);
                index = value >= 0 && value <= 255 ? static_cast<size_t>(value) : 256;
            } else if (pdfioArrayGetType(differences, i) == PDFIO_VALTYPE_NAME && index < map.simple.size()) {
                const char* name = pdfioArrayGetName(differences, i);
                map.simple[index++] = name == nullptr ? 0 : glyphNameCodepoint(name);
            }
        }
    }

    size_t tokenCount(std::string_view token) {
        size_t count = 0;
        const auto [end, error] = std::from_chars(token.data(), token.data() + token.size(), count);
        return error == std::errc{} && end == token.data() + token.size() ? count : 0;
    }

    bool readToken(pdfio_stream_t* stream, std::array<char, 256>& buffer, std::string_view& token) {
        if (!pdfioStreamGetToken(stream, buffer.data(), buffer.size()))
            return false;
        token = buffer.data();
        return true;
    }

    void loadToUnicode(pdfio_dict_t* resource, CharacterMap& map, CMapBudget& budget) {
        pdfio_obj_t* cmapObject = pdfioDictGetObj(resource, "ToUnicode");
        if (cmapObject == nullptr)
            return;
        pdfio_stream_t* stream = pdfioObjOpenStream(cmapObject, true);
        if (stream == nullptr)
            return;

        std::array<char, 256> buffer{};
        std::string_view token;
        size_t pendingCount = 0;
        std::string decoded;
        decoded.reserve(kMaximumMappingBytes);
        while (readToken(stream, buffer, token) && map.unicode.size() < kMaximumCMapEntries) {
            const size_t count = tokenCount(token);
            if (count > 0) {
                pendingCount = count;
                continue;
            }
            if (token == "beginbfchar") {
                for (size_t i = 0; i < pendingCount; ++i) {
                    if (!readToken(stream, buffer, token))
                        break;
                    uint32_t source = 0;
                    size_t sourceBytes = 0;
                    const bool validSource = parseSourceCode(token, source, sourceBytes);
                    if (!readToken(stream, buffer, token))
                        break;
                    if (validSource && decodeUtf16Hex(token, decoded)) {
                        map.codeBytes = std::max(map.codeBytes, sourceBytes);
                        map.add(source, decoded, budget);
                    }
                }
                pendingCount = 0;
            } else if (token == "beginbfrange") {
                for (size_t i = 0; i < pendingCount; ++i) {
                    uint32_t first = 0;
                    uint32_t last = 0;
                    size_t firstBytes = 0;
                    size_t lastBytes = 0;
                    if (!readToken(stream, buffer, token) || !parseSourceCode(token, first, firstBytes)
                        || !readToken(stream, buffer, token) || !parseSourceCode(token, last, lastBytes)
                        || !readToken(stream, buffer, token))
                        break;
                    if (first > last || firstBytes != lastBytes || last - first >= kMaximumCMapEntries)
                        continue;
                    map.codeBytes = std::max(map.codeBytes, firstBytes);
                    if (token == "[") {
                        for (uint32_t source = first; source <= last; ++source) {
                            if (!readToken(stream, buffer, token) || token == "]")
                                break;
                            if (decodeUtf16Hex(token, decoded))
                                map.add(source, decoded, budget);
                        }
                        while (token != "]" && readToken(stream, buffer, token)) {}
                    } else {
                        uint32_t firstCodepoint = 0;
                        if (!decodeSingleUtf16Codepoint(token, firstCodepoint))
                            continue;
                        for (uint32_t source = first; source <= last; ++source) {
                            decoded.clear();
                            if (appendUtf8(decoded, firstCodepoint + (source - first)))
                                map.add(source, decoded, budget);
                        }
                    }
                }
                pendingCount = 0;
            }
        }
        pdfioStreamClose(stream);
        map.finish();
    }

    CharacterMap characterMapFor(pdfio_obj_t* page, std::string_view name, CMapBudget& budget) {
        CharacterMap map;
        initializeWinAnsi(map);
        pdfio_dict_t* resource = characterResource(page, name);
        if (resource == nullptr)
            return map;

        const char* subtype = pdfioDictGetName(resource, "Subtype");
        const char* encodingName = pdfioDictGetName(resource, "Encoding");
        if (encodingName == nullptr) {
            pdfio_dict_t* encoding = pdfioDictGetDict(resource, "Encoding");
            if (encoding == nullptr) {
                if (pdfio_obj_t* object = pdfioDictGetObj(resource, "Encoding"))
                    encoding = pdfioObjGetDict(object);
            }
            if (encoding != nullptr)
                encodingName = pdfioDictGetName(encoding, "BaseEncoding");
        }
        map.composite = subtype != nullptr && std::strcmp(subtype, "Type0") == 0;
        if (encodingName != nullptr
            && (std::strcmp(encodingName, "Identity-H") == 0 || std::strcmp(encodingName, "Identity-V") == 0)) {
            map.composite = true;
            map.codeBytes = 2;
        }
        if (encodingName == nullptr || std::strcmp(encodingName, "WinAnsiEncoding") != 0)
            std::fill(map.simple.begin() + 128, map.simple.end(), 0);
        applyDifferences(resource, map);
        loadToUnicode(resource, map, budget);
        return map;
    }

    struct NamedCharacterMap {
        std::string name;
        CharacterMap characters;
    };

    std::vector<NamedCharacterMap> characterMapsFor(pdfio_obj_t* page) {
        std::vector<NamedCharacterMap> maps;
        pdfio_dict_t* resources = characterResources(page);
        if (resources == nullptr)
            return maps;
        const size_t count = std::min(pdfioDictGetNumPairs(resources), kMaximumPageCharacterMaps);
        maps.reserve(count);
        CMapBudget budget;
        for (size_t index = 0; index < count; ++index) {
            const char* name = pdfioDictGetKey(resources, index);
            if (name != nullptr)
                maps.push_back({.name = name, .characters = characterMapFor(page, name, budget)});
        }
        return maps;
    }

    const CharacterMap* findCharacterMap(const std::vector<NamedCharacterMap>& maps, std::string_view name) {
        const auto found = std::ranges::find(maps, name, &NamedCharacterMap::name);
        return found == maps.end() ? nullptr : &found->characters;
    }

    bool decodeBytes(const CharacterMap& map, std::span<const uint8_t> bytes, std::string& output) {
        const size_t codeBytes = map.unicode.empty() ? (map.composite ? 2 : 1) : map.codeBytes;
        if (codeBytes == 0 || bytes.size() % codeBytes != 0)
            return false;
        for (size_t offset = 0; offset < bytes.size(); offset += codeBytes) {
            const uint32_t code = sourceCode(bytes.subspan(offset, codeBytes));
            if (!map.unicode.empty()) {
                const std::string_view mapped = map.mappedText(code);
                if (mapped.empty())
                    return false;
                output += mapped;
            } else if (map.composite) {
                return false;
            } else if (code >= map.simple.size() || !appendUtf8(output, map.simple[code])) {
                return false;
            }
        }
        return true;
    }

    bool decodeHexText(const CharacterMap& map, std::string_view token, std::string& output) {
        const std::string_view payload = hexPayload(token);
        const size_t codeBytes = map.unicode.empty() ? (map.composite ? 2 : 1) : map.codeBytes;
        if (codeBytes == 0 || ((payload.size() + 1) / 2) % codeBytes != 0)
            return false;
        size_t position = 0;
        std::array<uint8_t, sizeof(uint32_t)> code{};
        while (position < payload.size()) {
            for (size_t i = 0; i < codeBytes; ++i) {
                if (!nextHexByte(payload, position, code[i]))
                    return false;
            }
            if (!decodeBytes(map, std::span{code}.first(codeBytes), output))
                return false;
        }
        return true;
    }

    bool decodeTextToken(const CharacterMap& map, std::string_view token, std::string& output) {
        if (token.empty())
            return true;
        if (token.front() == '(') {
            token.remove_prefix(1);
            return decodeBytes(map, {reinterpret_cast<const uint8_t*>(token.data()), token.size()}, output);
        }
        return token.front() == '<' ? decodeHexText(map, token, output) : true;
    }

} // namespace

std::expected<PdfTextExtractor, std::error_code> PdfTextExtractor::open(std::string_view path) {
    const std::string ownedPath{path};
    pdfio_file_t* file = pdfioFileOpen(ownedPath.c_str(), nullptr, nullptr, logPdfError, nullptr);
    if (file == nullptr)
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    return PdfTextExtractor{file};
}

PdfTextExtractor::PdfTextExtractor(pdfio_file_t* file) : file_(file) {
    const char* title = pdfioFileGetTitle(file_);
    const char* author = pdfioFileGetAuthor(file_);
    metadata_.title = title == nullptr ? "" : title;
    metadata_.author = author == nullptr ? "" : author;
}

PdfTextExtractor::PdfTextExtractor(PdfTextExtractor&& other) noexcept :
        file_(std::exchange(other.file_, nullptr)), metadata_(std::move(other.metadata_)) {}

PdfTextExtractor& PdfTextExtractor::operator=(PdfTextExtractor&& other) noexcept {
    if (this == &other)
        return *this;
    if (file_ != nullptr)
        pdfioFileClose(file_);
    file_ = std::exchange(other.file_, nullptr);
    metadata_ = std::move(other.metadata_);
    return *this;
}

PdfTextExtractor::~PdfTextExtractor() {
    if (file_ != nullptr)
        pdfioFileClose(file_);
}

const PdfTextExtractor::Metadata& PdfTextExtractor::metadata() const noexcept {
    return metadata_;
}

size_t PdfTextExtractor::pageCount() const noexcept {
    return file_ == nullptr ? 0 : pdfioFileGetNumPages(file_);
}

std::expected<void, std::error_code> PdfTextExtractor::extractPage(size_t pageIndex, LineCallback callback,
                                                                   void* context) {
    if (file_ == nullptr || callback == nullptr || pageIndex >= pageCount())
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    pdfio_obj_t* page = pdfioFileGetPage(file_, pageIndex);
    if (page == nullptr)
        return std::unexpected(std::make_error_code(std::errc::io_error));

    const std::vector<NamedCharacterMap> characterMaps = characterMapsFor(page);
    CMapBudget fallbackBudget;
    const CharacterMap fallbackMap = characterMapFor(page, {}, fallbackBudget);
    CharacterMap unsupportedMap;
    unsupportedMap.composite = true;
    const CharacterMap* activeMap = &fallbackMap;
    std::string activeResource;
    std::string pendingResource;
    std::string line;
    line.reserve(256);
    bool inText = false;
    bool inArray = false;
    bool firstArrayText = true;

    const auto selectCharacterMap = [&](std::string_view resource) {
        if (resource.empty())
            return &fallbackMap;
        const CharacterMap* selected = findCharacterMap(characterMaps, resource);
        return selected == nullptr ? &unsupportedMap : selected;
    };

    auto flushLine = [&]() {
        if (line.empty())
            return true;
        const bool accepted = callback(context, line);
        line.clear();
        return accepted;
    };
    auto boundLine = [&]() {
        if (line.size() <= kMaximumBufferedLineBytes)
            return true;
        const size_t split = line.find_last_of(" \t\r\n", kMaximumBufferedLineBytes);
        if (split == std::string::npos)
            return flushLine();
        const std::string_view prefix{line.data(), split};
        if (!callback(context, prefix))
            return false;
        line.erase(0, split + 1);
        return true;
    };

    std::array<char, kTokenBytes> buffer{};
    for (size_t streamIndex = 0; streamIndex < pdfioPageGetNumStreams(page); ++streamIndex) {
        pdfio_stream_t* stream = pdfioPageOpenStream(page, streamIndex, true);
        if (stream == nullptr)
            continue;
        while (pdfioStreamGetToken(stream, buffer.data(), buffer.size())) {
            const std::string_view token{buffer.data()};
            if (token == "BT") {
                inText = true;
            } else if (token == "ET") {
                if (!flushLine()) {
                    pdfioStreamClose(stream);
                    return std::unexpected(std::make_error_code(std::errc::operation_canceled));
                }
                inText = false;
            } else if (!inText) {
                continue;
            } else if (token == "[") {
                inArray = true;
                firstArrayText = true;
            } else if (token == "]") {
                inArray = false;
            } else if (token.starts_with('/')) {
                pendingResource.assign(token.substr(1));
            } else if (token == "Tf" && pendingResource != activeResource) {
                activeMap = selectCharacterMap(pendingResource);
                activeResource = pendingResource;
            } else if (token.starts_with('(') || token.starts_with('<')) {
                if (activeResource.empty()) {
                    activeMap = selectCharacterMap(pendingResource);
                    activeResource = pendingResource;
                }
                if (!decodeTextToken(*activeMap, token, line) || !boundLine()) {
                    pdfioStreamClose(stream);
                    return std::unexpected(std::make_error_code(std::errc::not_supported));
                }
                firstArrayText = false;
            } else if (inArray && !firstArrayText
                       && (token.starts_with('-') || (token.front() >= '0' && token.front() <= '9'))
                       && std::fabs(std::strtod(token.data(), nullptr)) > 100.0 && !line.ends_with(' ')) {
                line += ' ';
            } else if (token == "Td" || token == "TD" || token == "T*" || token == "'" || token == "\"") {
                if (!flushLine()) {
                    pdfioStreamClose(stream);
                    return std::unexpected(std::make_error_code(std::errc::operation_canceled));
                }
            }
        }
        pdfioStreamClose(stream);
    }

    if (!flushLine())
        return std::unexpected(std::make_error_code(std::errc::operation_canceled));
    return {};
}
