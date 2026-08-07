#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <hb.h>

#include "text/Utf8Text.h"

namespace UnicodeText {

    inline hb_unicode_funcs_t* unicodeFunctions() {
        static hb_unicode_funcs_t* const functions = hb_unicode_funcs_get_default();
        return functions;
    }

    inline hb_unicode_general_category_t generalCategory(uint32_t codepoint) {
        return hb_unicode_general_category(unicodeFunctions(), codepoint);
    }

    enum Script : uint32_t {
        ScriptNone = 0,
        ScriptLatin = 1U << 0U,
        ScriptCyrillic = 1U << 1U,
        ScriptGreek = 1U << 2U,
        ScriptHebrew = 1U << 3U,
        ScriptArabic = 1U << 4U,
        ScriptHan = 1U << 5U,
        ScriptHiragana = 1U << 6U,
        ScriptKatakana = 1U << 7U,
        ScriptHangul = 1U << 8U,
        ScriptMath = 1U << 9U,
    };

    struct ScriptTag {
        uint32_t mask;
        std::string_view tag;
    };

    inline constexpr std::array SupportedScripts{
        ScriptTag{ScriptLatin, "Latn"},       ScriptTag{ScriptCyrillic, "Cyrl"},
        ScriptTag{ScriptGreek, "Grek"},       ScriptTag{ScriptHebrew, "Hebr"},
        ScriptTag{ScriptArabic, "Arab"},      ScriptTag{ScriptHan, "Hani"},
        ScriptTag{ScriptHiragana, "Hira"},    ScriptTag{ScriptKatakana, "Kana"},
        ScriptTag{ScriptHangul, "Hang"},       ScriptTag{ScriptMath, "Zmth"},
    };

    enum Capability : uint32_t {
        CapabilityNone = 0,
        CapabilityBidi = 1U << 0U,
        CapabilityShaping = 1U << 1U,
        CapabilityCjkSegmentation = 1U << 2U,
        CapabilityMathSymbols = 1U << 3U,
    };

    struct CapabilityTag {
        uint32_t mask;
        std::string_view tag;
    };

    inline constexpr std::array SupportedCapabilities{
        CapabilityTag{CapabilityBidi, "bidi"},
        CapabilityTag{CapabilityShaping, "shaping.opentype"},
        CapabilityTag{CapabilityCjkSegmentation, "segmentation.cjk"},
        CapabilityTag{CapabilityMathSymbols, "math-symbols"},
    };

    constexpr uint32_t capabilityMask(std::string_view capability) {
        for (const CapabilityTag& supported: SupportedCapabilities) {
            if (capability == supported.tag)
                return supported.mask;
        }
        return CapabilityNone;
    }

    constexpr uint32_t scriptMask(std::string_view script) {
        for (const ScriptTag& supported: SupportedScripts) {
            if (script == supported.tag)
                return supported.mask;
        }
        if (script == "Jpan")
            return ScriptHan | ScriptHiragana | ScriptKatakana;
        if (script == "Kore")
            return ScriptHan | ScriptHangul;
        return ScriptNone;
    }

    constexpr bool isAsciiWhitespace(uint32_t codepoint) {
        return codepoint == ' ' || codepoint == '\t' || codepoint == '\n' || codepoint == '\r' || codepoint == '\f'
            || codepoint == '\v';
    }

    inline bool isWhitespace(uint32_t codepoint) {
        if (isAsciiWhitespace(codepoint) || codepoint == 0x0085U)
            return true;
        const auto category = generalCategory(codepoint);
        return category == HB_UNICODE_GENERAL_CATEGORY_SPACE_SEPARATOR
            || category == HB_UNICODE_GENERAL_CATEGORY_LINE_SEPARATOR
            || category == HB_UNICODE_GENERAL_CATEGORY_PARAGRAPH_SEPARATOR;
    }

    inline bool isDigit(uint32_t codepoint) {
        return generalCategory(codepoint) == HB_UNICODE_GENERAL_CATEGORY_DECIMAL_NUMBER;
    }

    constexpr bool isMathSymbol(uint32_t codepoint) {
        return (codepoint >= 0x2100U && codepoint <= 0x214FU) || (codepoint >= 0x2200U && codepoint <= 0x22FFU)
            || (codepoint >= 0x27C0U && codepoint <= 0x27EFU)
            || (codepoint >= 0x2980U && codepoint <= 0x2AFFU)
            || (codepoint >= 0x1D400U && codepoint <= 0x1D7FFU);
    }

    inline uint32_t scriptMask(uint32_t codepoint) {
        if (isMathSymbol(codepoint))
            return ScriptMath;
        switch (hb_unicode_script(unicodeFunctions(), codepoint)) {
        case HB_SCRIPT_LATIN: return ScriptLatin;
        case HB_SCRIPT_CYRILLIC: return ScriptCyrillic;
        case HB_SCRIPT_GREEK: return ScriptGreek;
        case HB_SCRIPT_HEBREW: return ScriptHebrew;
        case HB_SCRIPT_ARABIC: return ScriptArabic;
        case HB_SCRIPT_HAN: return ScriptHan;
        case HB_SCRIPT_HIRAGANA: return ScriptHiragana;
        case HB_SCRIPT_KATAKANA: return ScriptKatakana;
        case HB_SCRIPT_HANGUL: return ScriptHangul;
        default: return ScriptNone;
        }
    }

    inline uint32_t scriptsIn(std::string_view text) {
        uint32_t scripts = ScriptNone;
        uint32_t codepoint = 0;
        while (Utf8Text::next(text, codepoint))
            scripts |= scriptMask(codepoint);
        return scripts;
    }

    constexpr bool isCjkScript(uint32_t script) {
        return (script & (ScriptHan | ScriptHiragana | ScriptKatakana)) != 0;
    }

    inline bool isLetter(uint32_t codepoint) {
        switch (generalCategory(codepoint)) {
        case HB_UNICODE_GENERAL_CATEGORY_LOWERCASE_LETTER:
        case HB_UNICODE_GENERAL_CATEGORY_MODIFIER_LETTER:
        case HB_UNICODE_GENERAL_CATEGORY_OTHER_LETTER:
        case HB_UNICODE_GENERAL_CATEGORY_TITLECASE_LETTER:
        case HB_UNICODE_GENERAL_CATEGORY_UPPERCASE_LETTER: return true;
        default: return false;
        }
    }

    inline uint32_t capabilityMask(uint32_t codepoint) {
        const uint32_t script = scriptMask(codepoint);
        uint32_t capabilities = CapabilityNone;
        if ((script & (ScriptHebrew | ScriptArabic)) != 0)
            capabilities |= CapabilityBidi;
        if ((script & ScriptArabic) != 0 && isLetter(codepoint))
            capabilities |= CapabilityShaping;
        if ((script & (ScriptHan | ScriptHiragana | ScriptKatakana)) != 0)
            capabilities |= CapabilityCjkSegmentation;
        if (isMathSymbol(codepoint))
            capabilities |= CapabilityMathSymbols;
        return capabilities;
    }

    inline bool isWordCharacter(uint32_t codepoint) {
        return isLetter(codepoint) || isDigit(codepoint);
    }

    inline bool isCjkText(std::string_view text) {
        bool found = false;
        uint32_t codepoint = 0;
        while (Utf8Text::next(text, codepoint)) {
            const uint32_t script = scriptMask(codepoint);
            if (isCjkScript(script)) {
                found = true;
            } else if (isWordCharacter(codepoint)) {
                return false;
            }
        }
        return found;
    }

    inline bool isUppercaseLetter(uint32_t codepoint) {
        return generalCategory(codepoint) == HB_UNICODE_GENERAL_CATEGORY_UPPERCASE_LETTER;
    }

    inline bool isLowercaseLetter(uint32_t codepoint) {
        return generalCategory(codepoint) == HB_UNICODE_GENERAL_CATEGORY_LOWERCASE_LETTER;
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
