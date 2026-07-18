#include "text/TextNormalizer.h"

#include <cstdint>
#include <string_view>

#include "text/AsciiText.h"
#include "text/LatinText.h"

namespace {

    struct NamedEntity {
        const char* name;
        uint32_t codepoint;
    };

    bool parseNumericEntity(const String& entity, uint32_t& codepoint) {
        if (!entity.startsWith("#")) {
            return false;
        }

        std::string_view text{entity.c_str() + 1, entity.length() - 1};
        int base = 10;
        if (!text.empty() && (text.front() == 'x' || text.front() == 'X')) {
            text.remove_prefix(1);
            base = 16;
        }
        const auto parsed = AsciiText::parseUnsigned<uint32_t>(text, base);
        if (!parsed || *parsed > 0x10FFFF || (*parsed >= 0xD800 && *parsed <= 0xDFFF))
            return false;
        codepoint = *parsed;
        return true;
    }

    bool namedEntityCodepoint(const String& entity, uint32_t& codepoint) {
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

        for (const NamedEntity& entry: kEntities) {
            if (entity == entry.name) {
                codepoint = entry.codepoint;
                return true;
            }
        }
        return false;
    }

} // namespace

namespace RsvpText {

    void trimAsciiWhitespace(String& text) {
        size_t start = 0;
        while (start < text.length() && AsciiText::isWhitespace(text[start])) {
            ++start;
        }

        size_t end = text.length();
        while (end > start && AsciiText::isWhitespace(text[end - 1])) {
            --end;
        }

        if (end < text.length()) {
            text.remove(end);
        }
        if (start > 0) {
            text.remove(0, start);
        }
    }

    bool isUtf8Continuation(uint8_t value) {
        return (value & 0xC0) == 0x80;
    }

    bool decodeUtf8Codepoint(const String& text, size_t& index, uint32_t& codepoint) {
        const uint8_t first = static_cast<uint8_t>(text[index++]);
        if (first < 0x80) {
            codepoint = first;
            return true;
        }

        uint8_t continuationCount = 0;
        uint32_t minimumValue = 0;
        if ((first & 0xE0) == 0xC0) {
            codepoint = first & 0x1F;
            continuationCount = 1;
            minimumValue = 0x80;
        } else if ((first & 0xF0) == 0xE0) {
            codepoint = first & 0x0F;
            continuationCount = 2;
            minimumValue = 0x800;
        } else if ((first & 0xF8) == 0xF0) {
            codepoint = first & 0x07;
            continuationCount = 3;
            minimumValue = 0x10000;
        } else {
            return false;
        }

        if (index + continuationCount > text.length()) {
            return false;
        }

        for (uint8_t i = 0; i < continuationCount; ++i) {
            const uint8_t next = static_cast<uint8_t>(text[index]);
            if (!isUtf8Continuation(next)) {
                return false;
            }
            ++index;
            codepoint = (codepoint << 6) | (next & 0x3F);
        }

        if (codepoint < minimumValue || codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
            return false;
        }

        return true;
    }

    void appendDisplayApproximation(String& target, uint32_t codepoint) {
        if (codepoint >= 32 && codepoint <= 126) {
            target += static_cast<char>(codepoint);
            return;
        }

        if (codepoint == '\t' || codepoint == '\n' || codepoint == '\r' || codepoint == 0x00A0 || codepoint == 0x1680
            || codepoint == 0x180E || codepoint == 0x2028 || codepoint == 0x2029 || codepoint == 0x202F
            || codepoint == 0x205F || codepoint == 0x3000 || (codepoint >= 0x2000 && codepoint <= 0x200A)) {
            target += ' ';
            return;
        }

        if (codepoint == 0x00AD) {
            return;
        }

        uint8_t storedByte = 0;
        if (LatinText::storageByteForCodepoint(codepoint, storedByte)) {
            target += static_cast<char>(storedByte);
            return;
        }

        if (codepoint >= 0xFF01 && codepoint <= 0xFF5E) {
            target += static_cast<char>(codepoint - 0xFEE0);
            return;
        }

        switch (codepoint) {
        case 0x00A1:
            target += '!';
            return;
        case 0x00A2:
            target += 'c';
            return;
        case 0x00A3:
            target += "GBP";
            return;
        case 0x00A4:
            target += '$';
            return;
        case 0x00A5:
            target += 'Y';
            return;
        case 0x00A6:
            target += '|';
            return;
        case 0x00A7:
            target += 'S';
            return;
        case 0x00A8:
            target += '"';
            return;
        case 0x00A9:
            target += "(c)";
            return;
        case 0x00AA:
            target += 'a';
            return;
        case 0x00AB:
            target += '"';
            return;
        case 0x00AC:
            target += '!';
            return;
        case 0x00AE:
            target += "(r)";
            return;
        case 0x00AF:
            target += '-';
            return;
        case 0x00B0:
            target += "deg";
            return;
        case 0x00B1:
            target += "+/-";
            return;
        case 0x00B2:
            target += '2';
            return;
        case 0x00B3:
            target += '3';
            return;
        case 0x00B4:
            target += '\'';
            return;
        case 0x00B5:
            target += 'u';
            return;
        case 0x00B6:
            target += 'P';
            return;
        case 0x00B7:
            target += '*';
            return;
        case 0x00B8:
            target += ',';
            return;
        case 0x00B9:
            target += '1';
            return;
        case 0x00BA:
            target += 'o';
            return;
        case 0x00BB:
            target += '"';
            return;
        case 0x2039:
        case 0x203A:
            target += '\'';
            return;
        case 0x00BC:
            target += "1/4";
            return;
        case 0x00BD:
            target += "1/2";
            return;
        case 0x00BE:
            target += "3/4";
            return;
        case 0x00BF:
            target += '?';
            return;
        case 0x2018:
        case 0x2019:
        case 0x201A:
        case 0x201B:
        case 0x2032:
        case 0x2035:
            target += '\'';
            return;
        case 0x201C:
        case 0x201D:
        case 0x201E:
        case 0x201F:
        case 0x2033:
        case 0x2036:
        case 0x300C:
        case 0x300D:
        case 0x300E:
        case 0x300F:
            target += '"';
            return;
        case 0x207D:
        case 0x208D:
        case 0x2768:
        case 0x276A:
        case 0xFF08:
            target += '(';
            return;
        case 0x207E:
        case 0x208E:
        case 0x2769:
        case 0x276B:
        case 0xFF09:
            target += ')';
            return;
        case 0x2045:
        case 0x2308:
        case 0x230A:
        case 0x3010:
        case 0x3014:
        case 0x3016:
        case 0x3018:
        case 0x301A:
        case 0xFF3B:
            target += '[';
            return;
        case 0x2046:
        case 0x2309:
        case 0x230B:
        case 0x3011:
        case 0x3015:
        case 0x3017:
        case 0x3019:
        case 0x301B:
        case 0xFF3D:
            target += ']';
            return;
        case 0x2774:
        case 0x2776:
        case 0xFF5B:
            target += '{';
            return;
        case 0x2775:
        case 0x2777:
        case 0xFF5D:
            target += '}';
            return;
        case 0x2329:
        case 0x27E8:
        case 0x3008:
        case 0x300A:
            target += '<';
            return;
        case 0x232A:
        case 0x27E9:
        case 0x3009:
        case 0x300B:
            target += '>';
            return;
        case 0x2010:
        case 0x2011:
            target += '-';
            return;
        case 0x2012:
        case 0x2013:
        case 0x2014:
        case 0x2015:
        case 0x2043:
            target += " - ";
            return;
        case 0x2212:
            target += '-';
            return;
        case 0x2026:
            target += "...";
            return;
        case 0x2022:
        case 0x2219:
            target += '*';
            return;
        case 0xFF0C:
            target += ',';
            return;
        case 0xFF0E:
            target += '.';
            return;
        case 0xFF1A:
            target += ':';
            return;
        case 0xFF1B:
            target += ';';
            return;
        case 0xFF01:
            target += '!';
            return;
        case 0xFF1F:
            target += '?';
            return;
        case 0x2122:
            target += "TM";
            return;
        case 0x00D7:
            target += 'x';
            return;
        case 0x00F7:
            target += '/';
            return;
        case 0x0100:
        case 0x0102:
            target += 'A';
            return;
        case 0x0101:
        case 0x0103:
            target += 'a';
            return;
        case 0x0108:
        case 0x010A:
        case 0x010C:
            target += 'C';
            return;
        case 0x0109:
        case 0x010B:
        case 0x010D:
            target += 'c';
            return;
        case 0x010E:
        case 0x0110:
            target += 'D';
            return;
        case 0x010F:
        case 0x0111:
            target += 'd';
            return;
        case 0x0112:
        case 0x0114:
        case 0x0116:
        case 0x011A:
            target += 'E';
            return;
        case 0x0113:
        case 0x0115:
        case 0x0117:
        case 0x011B:
            target += 'e';
            return;
        case 0x011C:
        case 0x011E:
        case 0x0120:
        case 0x0122:
            target += 'G';
            return;
        case 0x011D:
        case 0x011F:
        case 0x0121:
        case 0x0123:
            target += 'g';
            return;
        case 0x0124:
        case 0x0126:
            target += 'H';
            return;
        case 0x0125:
        case 0x0127:
            target += 'h';
            return;
        case 0x0128:
        case 0x012A:
        case 0x012C:
        case 0x012E:
        case 0x0130:
            target += 'I';
            return;
        case 0x0129:
        case 0x012B:
        case 0x012D:
        case 0x012F:
        case 0x0131:
            target += 'i';
            return;
        case 0x0134:
            target += 'J';
            return;
        case 0x0135:
            target += 'j';
            return;
        case 0x0136:
            target += 'K';
            return;
        case 0x0137:
            target += 'k';
            return;
        case 0x0139:
        case 0x013B:
        case 0x013D:
        case 0x013F:
            target += 'L';
            return;
        case 0x013A:
        case 0x013C:
        case 0x013E:
        case 0x0140:
            target += 'l';
            return;
        case 0x0145:
        case 0x0147:
            target += 'N';
            return;
        case 0x0146:
        case 0x0148:
            target += 'n';
            return;
        case 0x014C:
        case 0x014E:
        case 0x0150:
            target += 'O';
            return;
        case 0x014D:
        case 0x014F:
        case 0x0151:
            target += 'o';
            return;
        case 0x0152:
            target += "OE";
            return;
        case 0x0153:
            target += "oe";
            return;
        case 0x0154:
        case 0x0156:
        case 0x0158:
            target += 'R';
            return;
        case 0x0155:
        case 0x0157:
        case 0x0159:
            target += 'r';
            return;
        case 0x015C:
        case 0x015E:
        case 0x0160:
            target += 'S';
            return;
        case 0x015D:
        case 0x015F:
        case 0x0161:
            target += 's';
            return;
        case 0x0162:
        case 0x0164:
        case 0x0166:
            target += 'T';
            return;
        case 0x0163:
        case 0x0165:
        case 0x0167:
            target += 't';
            return;
        case 0x0168:
        case 0x016A:
        case 0x016C:
        case 0x016E:
        case 0x0170:
        case 0x0172:
            target += 'U';
            return;
        case 0x0169:
        case 0x016B:
        case 0x016D:
        case 0x016F:
        case 0x0171:
        case 0x0173:
            target += 'u';
            return;
        case 0x0174:
            target += 'W';
            return;
        case 0x0175:
            target += 'w';
            return;
        case 0x0176:
        case 0x0178:
            target += 'Y';
            return;
        case 0x0177:
            target += 'y';
            return;
        case 0x017D:
            target += 'Z';
            return;
        case 0x017E:
            target += 'z';
            return;
        case 0x01E2:
        case 0x01FC:
            target += "AE";
            return;
        case 0x01E3:
        case 0x01FD:
            target += "ae";
            return;
        case 0xFB00:
            target += "ff";
            return;
        case 0xFB01:
            target += "fi";
            return;
        case 0xFB02:
            target += "fl";
            return;
        case 0xFB03:
            target += "ffi";
            return;
        case 0xFB04:
            target += "ffl";
            return;
        case 0xFB05:
        case 0xFB06:
            target += "st";
            return;
        default:
            return;
        }
    }

    bool decodeMarkupEntity(const String& entity, String& decoded) {
        uint32_t codepoint = 0;
        if (!parseNumericEntity(entity, codepoint) && !namedEntityCodepoint(entity, codepoint)) {
            return false;
        }

        decoded = "";
        appendDisplayApproximation(decoded, codepoint);
        return !decoded.isEmpty() || codepoint == 0x00AD || codepoint == 0x200B || codepoint == 0xFEFF;
    }

    String decodeMarkupEntities(const String& text) {
        String output;
        output.reserve(text.length());

        for (size_t i = 0; i < text.length(); ++i) {
            if (text[i] != '&') {
                output += text[i];
                continue;
            }

            const int entityEnd = text.indexOf(';', i + 1);
            if (entityEnd <= 0 || entityEnd - static_cast<int>(i) > 32) {
                output += '&';
                continue;
            }

            String decoded;
            if (!decodeMarkupEntity(text.substring(i + 1, entityEnd), decoded)) {
                output += '&';
                continue;
            }

            output += decoded;
            i = static_cast<size_t>(entityEnd);
        }

        return output;
    }

    void appendSingleByteApproximation(String& target, uint8_t value) {
        switch (value) {
        case 0xA0:
            target += ' ';
            return;
        case 0xA1:
            target += static_cast<char>(0x96);
            return;
        case 0xA2:
            target += 'c';
            return;
        case 0xA3:
            target += static_cast<char>(0x82);
            return;
        case 0xA4:
            target += '$';
            return;
        case 0xA5:
            target += 'Y';
            return;
        case 0xA6:
            target += static_cast<char>(0x9E);
            return;
        case 0xA7:
            target += 'S';
            return;
        case 0xA8:
            target += '"';
            return;
        case 0xA9:
            target += "(c)";
            return;
        case 0xAA:
            target += 'a';
            return;
        case 0xAB:
            target += '"';
            return;
        case 0xAD:
            return;
        case 0xAC:
            target += '!';
            return;
        case 0xAE:
            target += static_cast<char>(0xB4);
            return;
        case 0xAF:
            target += static_cast<char>(0xB2);
            return;
        case 0xB0:
            target += "deg";
            return;
        case 0xB1:
            target += static_cast<char>(0x97);
            return;
        case 0x80:
            target += "EUR";
            return;
        case 0x8A:
            target += static_cast<char>(0x86);
            return;
        case 0x8C:
            target += static_cast<char>(0x80);
            return;
        case 0x8E:
            target += static_cast<char>(0x88);
            return;
        case 0x82:
        case 0x91:
        case 0x92:
            target += '\'';
            return;
        case 0x84:
        case 0x93:
        case 0x94:
            target += '"';
            return;
        case 0x85:
            target += "...";
            return;
        case 0x95:
            target += '*';
            return;
        case 0x96:
        case 0x97:
            target += '-';
            return;
        case 0x99:
            target += "TM";
            return;
        case 0x9A:
            target += static_cast<char>(0x87);
            return;
        case 0x9C:
            target += static_cast<char>(0x81);
            return;
        case 0x9E:
            target += static_cast<char>(0x89);
            return;
        case 0x9F:
            target += 'Y';
            return;
        case 0xB2:
            target += '2';
            return;
        case 0xB3:
            target += static_cast<char>(0x83);
            return;
        case 0xB4:
            target += '\'';
            return;
        case 0xB5:
            target += 'u';
            return;
        case 0xB6:
            target += static_cast<char>(0x9F);
            return;
        case 0xB7:
            target += '*';
            return;
        case 0xB8:
            target += ',';
            return;
        case 0xB9:
            target += '1';
            return;
        case 0xBA:
            target += 'o';
            return;
        case 0xBB:
            target += '"';
            return;
        case 0xBC:
            target += "1/4";
            return;
        case 0xBD:
            target += "1/2";
            return;
        case 0xBE:
            target += static_cast<char>(0xB5);
            return;
        case 0xBF:
            target += static_cast<char>(0xB3);
            return;
        case 0xC6:
            target += static_cast<char>(0x9A);
            return;
        case 0xCA:
            target += static_cast<char>(0x98);
            return;
        case 0xD1:
            target += static_cast<char>(0x9C);
            return;
        case 0xD7:
            target += 'x';
            return;
        case 0xE6:
            target += static_cast<char>(0x9B);
            return;
        case 0xEA:
            target += static_cast<char>(0x99);
            return;
        case 0xF1:
            target += static_cast<char>(0x9D);
            return;
        case 0xF7:
            target += '/';
            return;
        default:
            if (value >= 0xA0) {
                target += static_cast<char>(value);
            }
            return;
        }
    }

    String normalizeDisplayText(const String& text, ParseStats* stats) {
        String normalized;
        normalized.reserve(text.length());

        size_t index = 0;
        while (index < text.length()) {
            const size_t before = index;
            uint32_t codepoint = 0;
            if (decodeUtf8Codepoint(text, index, codepoint)) {
                if (stats != nullptr && codepoint > 0x7F) {
                    ++stats->nonAsciiCodepoints;
                }
                appendDisplayApproximation(normalized, codepoint);
                continue;
            }

            if (stats != nullptr && static_cast<uint8_t>(text[before]) >= 0x80) {
                ++stats->malformedUtf8;
            }
            index = before + 1;
            const uint8_t rawByte = static_cast<uint8_t>(text[before]);
            if (LatinText::isWordCharacter(rawByte) || LatinText::isLowCustomSlotByte(rawByte)) {
                normalized += static_cast<char>(rawByte);
            } else {
                appendSingleByteApproximation(normalized, rawByte);
            }
        }

        String collapsed;
        collapsed.reserve(normalized.length());
        bool previousSpace = true;
        for (size_t i = 0; i < normalized.length(); ++i) {
            const uint8_t value = LatinText::byteValue(normalized[i]);
            if (value <= ' ' && !LatinText::isWordCharacter(value) && !LatinText::isLowCustomSlotByte(value)) {
                if (!previousSpace) {
                    collapsed += ' ';
                    previousSpace = true;
                }
                continue;
            }

            collapsed += static_cast<char>(value);
            previousSpace = false;
        }

        if (!collapsed.isEmpty() && collapsed[collapsed.length() - 1] == ' ') {
            collapsed.remove(collapsed.length() - 1, 1);
        }
        return collapsed;
    }

} // namespace RsvpText
