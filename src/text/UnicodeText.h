#pragma once

#include <cstdint>

namespace UnicodeText {

    // These helpers intentionally cover the scripts shipped in the UI font:
    // Basic Latin, Latin-1 Supplement, Latin Extended-A, and Cyrillic.

    constexpr bool isAsciiWhitespace(uint32_t codepoint) {
        return codepoint == ' ' || codepoint == '\t' || codepoint == '\n' || codepoint == '\r' || codepoint == '\f'
            || codepoint == '\v';
    }

    constexpr bool isWhitespace(uint32_t codepoint) {
        // Unicode whitespace outside ASCII: next-line/no-break, Ogham, the
        // U+2000 spacing block, line/paragraph separators, and wide spaces.
        return isAsciiWhitespace(codepoint) || codepoint == 0x0085U || codepoint == 0x00A0U || codepoint == 0x1680U
            || (codepoint >= 0x2000U && codepoint <= 0x200AU) || codepoint == 0x2028U || codepoint == 0x2029U
            || codepoint == 0x202FU || codepoint == 0x205FU || codepoint == 0x3000U;
    }

    constexpr bool isDigit(uint32_t codepoint) {
        return codepoint >= '0' && codepoint <= '9';
    }

    constexpr bool isLetter(uint32_t codepoint) {
        const bool basicLatin = (codepoint >= 'A' && codepoint <= 'Z') || (codepoint >= 'a' && codepoint <= 'z');
        // Latin-1 letters include the ordinal indicators and micro sign.
        const bool latin1 = codepoint == 0x00AAU || codepoint == 0x00B5U || codepoint == 0x00BAU
                         || (codepoint >= 0x00C0U && codepoint <= 0x00D6U)
                         || (codepoint >= 0x00D8U && codepoint <= 0x00F6U)
                         || (codepoint >= 0x00F8U && codepoint <= 0x00FFU);
        const bool latinExtendedA = codepoint >= 0x0100U && codepoint <= 0x017FU;
        const bool cyrillic =
            (codepoint >= 0x0400U && codepoint <= 0x0481U) || (codepoint >= 0x048AU && codepoint <= 0x04FFU);
        return basicLatin || latin1 || latinExtendedA || cyrillic;
    }

    constexpr bool isWordCharacter(uint32_t codepoint) {
        return isLetter(codepoint) || isDigit(codepoint);
    }

    constexpr bool isUppercaseLetter(uint32_t codepoint) {
        const bool basicLatin = codepoint >= 'A' && codepoint <= 'Z';
        const bool latin1 =
            (codepoint >= 0x00C0U && codepoint <= 0x00D6U) || (codepoint >= 0x00D8U && codepoint <= 0x00DEU);

        // Latin Extended-A mostly alternates uppercase/lowercase pairs. Two
        // subranges start on odd codepoints, and dotted I/diaeresis Y are exceptions.
        const bool latinExtendedA =
            (codepoint >= 0x0100U && codepoint <= 0x012EU && (codepoint & 1U) == 0)
            || (codepoint >= 0x0132U && codepoint <= 0x0136U && (codepoint & 1U) == 0) || codepoint == 0x0130U
            || (codepoint >= 0x0139U && codepoint <= 0x0147U && (codepoint & 1U) != 0)
            || (codepoint >= 0x014AU && codepoint <= 0x0176U && (codepoint & 1U) == 0) || codepoint == 0x0178U
            || (codepoint >= 0x0179U && codepoint <= 0x017DU && (codepoint & 1U) != 0);

        // Cyrillic uses contiguous uppercase blocks plus alternating extension pairs.
        const bool cyrillic = (codepoint >= 0x0400U && codepoint <= 0x042FU)
                           || (codepoint >= 0x0460U && codepoint <= 0x0480U && (codepoint & 1U) == 0)
                           || (codepoint >= 0x048AU && codepoint <= 0x04BEU && (codepoint & 1U) == 0)
                           || codepoint == 0x04C0U
                           || (codepoint >= 0x04C1U && codepoint <= 0x04CDU && (codepoint & 1U) != 0)
                           || (codepoint >= 0x04D0U && codepoint <= 0x04FEU && (codepoint & 1U) == 0);
        return basicLatin || latin1 || latinExtendedA || cyrillic;
    }

    constexpr bool isLowercaseLetter(uint32_t codepoint) {
        return isLetter(codepoint) && !isUppercaseLetter(codepoint);
    }

    constexpr uint32_t toLowercase(uint32_t codepoint) {
        // Basic Latin and Latin-1 uppercase letters have a fixed delta.
        if (codepoint >= 'A' && codepoint <= 'Z')
            return codepoint + ('a' - 'A');
        if ((codepoint >= 0x00C0U && codepoint <= 0x00D6U) || (codepoint >= 0x00D8U && codepoint <= 0x00DEU)) {
            return codepoint + 0x20U;
        }
        // Alternating Latin Extended-A and Cyrillic pairs map to the next codepoint.
        if ((codepoint >= 0x0100U && codepoint <= 0x012EU && (codepoint & 1U) == 0)
            || (codepoint >= 0x0132U && codepoint <= 0x0136U && (codepoint & 1U) == 0)
            || (codepoint >= 0x014AU && codepoint <= 0x0176U && (codepoint & 1U) == 0)
            || (codepoint >= 0x0460U && codepoint <= 0x0480U && (codepoint & 1U) == 0)
            || (codepoint >= 0x048AU && codepoint <= 0x04BEU && (codepoint & 1U) == 0)
            || (codepoint >= 0x04D0U && codepoint <= 0x04FEU && (codepoint & 1U) == 0)) {
            return codepoint + 1U;
        }
        if ((codepoint >= 0x0139U && codepoint <= 0x0147U && (codepoint & 1U) != 0)
            || (codepoint >= 0x0179U && codepoint <= 0x017DU && (codepoint & 1U) != 0)
            || (codepoint >= 0x04C1U && codepoint <= 0x04CDU && (codepoint & 1U) != 0)) {
            return codepoint + 1U;
        }
        // Cyrillic supplement and basic Cyrillic uppercase blocks use fixed deltas.
        if (codepoint >= 0x0400U && codepoint <= 0x040FU)
            return codepoint + 0x50U;
        if (codepoint >= 0x0410U && codepoint <= 0x042FU)
            return codepoint + 0x20U;
        // Dotted I, diaeresis Y, and Cyrillic palochka are isolated exceptions.
        if (codepoint == 0x0130U)
            return 'i';
        if (codepoint == 0x0178U)
            return 0x00FFU;
        if (codepoint == 0x04C0U)
            return 0x04CFU;
        return codepoint;
    }

    constexpr bool isVowel(uint32_t codepoint) {
        switch (toLowercase(codepoint)) {
        // Basic Latin vowels.
        case 'a':
        case 'e':
        case 'i':
        case 'o':
        case 'u':
        case 'y':
        // Latin-1 accented vowels and ligatures.
        case 0x00E0U:
        case 0x00E1U:
        case 0x00E2U:
        case 0x00E3U:
        case 0x00E4U:
        case 0x00E5U:
        case 0x00E6U:
        case 0x00E8U:
        case 0x00E9U:
        case 0x00EAU:
        case 0x00EBU:
        case 0x00ECU:
        case 0x00EDU:
        case 0x00EEU:
        case 0x00EFU:
        case 0x00F2U:
        case 0x00F3U:
        case 0x00F4U:
        case 0x00F5U:
        case 0x00F6U:
        case 0x00F8U:
        case 0x00F9U:
        case 0x00FAU:
        case 0x00FBU:
        case 0x00FCU:
        case 0x00FDU:
        case 0x00FFU:
        // Latin Extended-A vowel variants: a, e, i, o, u, and y.
        case 0x0101U:
        case 0x0103U:
        case 0x0105U:
        case 0x0113U:
        case 0x0115U:
        case 0x0117U:
        case 0x0119U:
        case 0x011BU:
        case 0x0129U:
        case 0x012BU:
        case 0x012DU:
        case 0x012FU:
        case 0x0131U:
        case 0x014DU:
        case 0x014FU:
        case 0x0151U:
        case 0x0153U:
        case 0x0169U:
        case 0x016BU:
        case 0x016DU:
        case 0x016FU:
        case 0x0171U:
        case 0x0173U:
        case 0x0177U:
        // Cyrillic vowels: а, е, и, о, у, ы, э, ю, я, ё, є, і, ї.
        case 0x0430U:
        case 0x0435U:
        case 0x0438U:
        case 0x043EU:
        case 0x0443U:
        case 0x044BU:
        case 0x044DU:
        case 0x044EU:
        case 0x044FU:
        case 0x0451U:
        case 0x0454U:
        case 0x0456U:
        case 0x0457U:
            return true;
        default:
            return false;
        }
    }

} // namespace UnicodeText
