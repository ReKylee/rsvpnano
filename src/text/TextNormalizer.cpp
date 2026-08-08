#include "text/TextNormalizer.h"

#include <algorithm>
#include <cstdint>
#include <string_view>

#include "text/AsciiText.h"
#include "text/UnicodeText.h"
#include "text/Utf8Text.h"

namespace {

    struct NamedEntity {
        const char* name;
        uint32_t codepoint;
    };

    bool parseNumericEntity(std::string_view entity, uint32_t& codepoint) {
        if (entity.empty() || entity.front() != '#') {
            return false;
        }

        std::string_view text = entity.substr(1);
        int base = 10;
        if (!text.empty() && (text.front() == 'x' || text.front() == 'X')) {
            text.remove_prefix(1);
            base = 16;
        }
        const auto parsed = AsciiText::parseUnsigned<uint32_t>(text, base);
        if (!parsed || !Utf8Text::isScalarValue(*parsed))
            return false;
        codepoint = *parsed;
        return true;
    }

    bool namedEntityCodepoint(std::string_view entity, uint32_t& codepoint) {
        static constexpr NamedEntity kEntities[] = {
            {"amp", '&'},       {"lt", '<'},
            {"gt", '>'},        {"quot", '"'},
            {"apos", '\''},     {"nbsp", 0x00A0},
            {"iexcl", 0x00A1},  {"iquest", 0x00BF},
            {"copy", 0x00A9},   {"reg", 0x00AE},
            {"deg", 0x00B0},    {"plusmn", 0x00B1},
            {"sup2", 0x00B2},   {"sup3", 0x00B3},
            {"sup1", 0x00B9},   {"frac14", 0x00BC},
            {"frac12", 0x00BD}, {"frac34", 0x00BE},
            {"laquo", 0x00AB},  {"raquo", 0x00BB},
            {"middot", 0x00B7}, {"bull", 0x2022},
            {"times", 0x00D7},  {"divide", 0x00F7},
            {"ndash", 0x2013},  {"mdash", 0x2014},
            {"hyphen", 0x2010}, {"minus", 0x2212},
            {"hellip", 0x2026}, {"lsquo", 0x2018},
            {"rsquo", 0x2019},  {"sbquo", 0x201A},
            {"ldquo", 0x201C},  {"rdquo", 0x201D},
            {"bdquo", 0x201E},  {"lsaquo", 0x2039},
            {"rsaquo", 0x203A}, {"lpar", '('},
            {"rpar", ')'},      {"lbrack", '['},
            {"rbrack", ']'},    {"lcub", '{'},
            {"rcub", '}'},

            {"Agrave", 0x00C0}, {"Aacute", 0x00C1},
            {"Acirc", 0x00C2},  {"Atilde", 0x00C3},
            {"Auml", 0x00C4},   {"Aring", 0x00C5},
            {"AElig", 0x00C6},  {"Ccedil", 0x00C7},
            {"Egrave", 0x00C8}, {"Eacute", 0x00C9},
            {"Ecirc", 0x00CA},  {"Euml", 0x00CB},
            {"Igrave", 0x00CC}, {"Iacute", 0x00CD},
            {"Icirc", 0x00CE},  {"Iuml", 0x00CF},
            {"ETH", 0x00D0},    {"Ntilde", 0x00D1},
            {"Ograve", 0x00D2}, {"Oacute", 0x00D3},
            {"Ocirc", 0x00D4},  {"Otilde", 0x00D5},
            {"Ouml", 0x00D6},   {"Oslash", 0x00D8},
            {"Ugrave", 0x00D9}, {"Uacute", 0x00DA},
            {"Ucirc", 0x00DB},  {"Uuml", 0x00DC},
            {"Yacute", 0x00DD}, {"THORN", 0x00DE},
            {"szlig", 0x00DF},  {"agrave", 0x00E0},
            {"aacute", 0x00E1}, {"acirc", 0x00E2},
            {"atilde", 0x00E3}, {"auml", 0x00E4},
            {"aring", 0x00E5},  {"aelig", 0x00E6},
            {"ccedil", 0x00E7}, {"egrave", 0x00E8},
            {"eacute", 0x00E9}, {"ecirc", 0x00EA},
            {"euml", 0x00EB},   {"igrave", 0x00EC},
            {"iacute", 0x00ED}, {"icirc", 0x00EE},
            {"iuml", 0x00EF},   {"eth", 0x00F0},
            {"ntilde", 0x00F1}, {"ograve", 0x00F2},
            {"oacute", 0x00F3}, {"ocirc", 0x00F4},
            {"otilde", 0x00F5}, {"ouml", 0x00F6},
            {"oslash", 0x00F8}, {"ugrave", 0x00F9},
            {"uacute", 0x00FA}, {"ucirc", 0x00FB},
            {"uuml", 0x00FC},   {"yacute", 0x00FD},
            {"thorn", 0x00FE},  {"yuml", 0x00FF},

            {"Dcaron", 0x010E}, {"dcaron", 0x010F},
            {"Ecaron", 0x011A}, {"ecaron", 0x011B},
            {"Ncaron", 0x0147}, {"ncaron", 0x0148},
            {"Rcaron", 0x0158}, {"rcaron", 0x0159},
            {"Tcaron", 0x0164}, {"tcaron", 0x0165},
            {"Uring", 0x016E},  {"uring", 0x016F},
            {"Odblac", 0x0150}, {"odblac", 0x0151},
            {"Udblac", 0x0170}, {"udblac", 0x0171},
            {"OElig", 0x0152},  {"oelig", 0x0153},
            {"Scaron", 0x0160}, {"scaron", 0x0161},
            {"Zcaron", 0x017D}, {"zcaron", 0x017E},
            {"Amacr", 0x0100},  {"amacr", 0x0101},
            {"Emacr", 0x0112},  {"emacr", 0x0113},
            {"Gcedil", 0x0122}, {"Gcommaaccent", 0x0122},
            {"gcedil", 0x0123}, {"gcommaaccent", 0x0123},
            {"Imacr", 0x012A},  {"imacr", 0x012B},
            {"Kcedil", 0x0136}, {"Kcommaaccent", 0x0136},
            {"kcedil", 0x0137}, {"kcommaaccent", 0x0137},
            {"Lcedil", 0x013B}, {"Lcommaaccent", 0x013B},
            {"lcedil", 0x013C}, {"lcommaaccent", 0x013C},
            {"Ncedil", 0x0145}, {"Ncommaaccent", 0x0145},
            {"ncedil", 0x0146}, {"ncommaaccent", 0x0146},
            {"Edot", 0x0116},   {"edot", 0x0117},
            {"Iogon", 0x012E},  {"iogon", 0x012F},
            {"Uogon", 0x0172},  {"uogon", 0x0173},
            {"Umacr", 0x016A},  {"umacr", 0x016B},
            {"Dstrok", 0x0110}, {"dstrok", 0x0111},
            {"ENG", 0x014A},    {"eng", 0x014B},
            {"Tstrok", 0x0166}, {"tstrok", 0x0167},
        };

        const auto entry = std::ranges::find_if(kEntities, [entity](const NamedEntity& candidate) {
            return entity == candidate.name;
        });
        if (entry == std::end(kEntities))
            return false;
        codepoint = entry->codepoint;
        return true;
    }

} // namespace

namespace RsvpText {

    static void appendNormalizedCodepoint(std::string& target, uint32_t codepoint) {
        if (UnicodeText::isWhitespace(codepoint)) {
            if (!target.empty() && target.back() != ' ')
                target += ' ';
            return;
        }
        // Drop soft hyphen, zero-width space, BOM, and C0/C1 controls.
        if (codepoint == 0x00ADU || codepoint == 0x200BU || codepoint == 0xFEFFU || codepoint < 0x20U
            || (codepoint >= 0x7FU && codepoint <= 0x9FU)) {
            return;
        }
        Utf8Text::append(target, codepoint);
    }

    bool decodeMarkupEntity(std::string_view entity, std::string& decoded) {
        uint32_t codepoint = 0;
        if (!parseNumericEntity(entity, codepoint) && !namedEntityCodepoint(entity, codepoint)) {
            return false;
        }

        decoded.clear();
        if (UnicodeText::isWhitespace(codepoint))
            decoded = " ";
        else
            appendNormalizedCodepoint(decoded, codepoint);
        return !decoded.empty() || codepoint == 0x00AD || codepoint == 0x200B || codepoint == 0xFEFF;
    }

    std::string decodeMarkupEntities(std::string_view text) {
        std::string output;
        output.reserve(text.length());

        for (size_t i = 0; i < text.length(); ++i) {
            if (text[i] != '&') {
                output += text[i];
                continue;
            }

            const size_t entityEnd = text.find(';', i + 1);
            if (entityEnd == std::string_view::npos || entityEnd - i > 32) {
                output += '&';
                continue;
            }

            std::string decoded;
            if (!decodeMarkupEntity(text.substr(i + 1, entityEnd - i - 1), decoded)) {
                output += '&';
                continue;
            }

            output += decoded;
            i = entityEnd;
        }

        return output;
    }

    std::string normalizeDisplayText(std::string_view text, NormalizationStats* stats) {
        std::string normalized;
        normalized.reserve(text.length());

        std::string_view remaining = text;
        while (!remaining.empty()) {
            const uint8_t first = static_cast<uint8_t>(remaining.front());
            uint32_t codepoint = 0;
            if (!Utf8Text::decode(remaining, codepoint)) {
                if (stats != nullptr && first >= 0x80U)
                    ++stats->malformedUtf8;
                appendNormalizedCodepoint(normalized, '?');
                continue;
            }

            if (stats != nullptr && codepoint > 0x7FU)
                ++stats->nonAsciiCodepoints;
            appendNormalizedCodepoint(normalized, codepoint);
        }

        if (!normalized.empty() && normalized.back() == ' ')
            normalized.pop_back();
        return normalized;
    }

    std::string uiSafeMetadata(std::string_view text) {
        const std::string normalized = normalizeDisplayText(text);
        std::string safe;
        safe.reserve(normalized.size());
        std::string_view remaining = normalized;
        uint32_t codepoint = 0;
        while (Utf8Text::next(remaining, codepoint)) {
            const uint32_t script = UnicodeText::scriptMask(codepoint);
            if (codepoint <= 0x7FU || (script & (UnicodeText::ScriptLatin | UnicodeText::ScriptCyrillic)) != 0)
                Utf8Text::append(safe, codepoint);
            else
                safe.push_back('?');
        }
        return safe;
    }

    std::string readableKey(std::string_view text) {
        const std::string normalized = normalizeDisplayText(text);
        std::string key;
        key.reserve(normalized.length());

        std::string_view remaining = normalized;
        uint32_t codepoint = 0;
        while (Utf8Text::next(remaining, codepoint)) {
            if (UnicodeText::isWordCharacter(codepoint))
                Utf8Text::append(key, UnicodeText::toLowercase(codepoint));
        }
        return key;
    }

} // namespace RsvpText
