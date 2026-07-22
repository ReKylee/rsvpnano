#include "text/RsvpTokenizer.h"

#include "text/UnicodeText.h"
#include "text/Utf8Text.h"

namespace RsvpText {

    namespace Detail {

        bool isWordBoundary(char c) {
            return static_cast<uint8_t>(c) <= ' ';
        }

        bool isInlineWordHyphen(std::string_view text, size_t index) {
            if (index == 0 || index + 1 >= text.length() || text[index] != '-') {
                return false;
            }
            if (text[index - 1] == '-' || text[index + 1] == '-') {
                return false;
            }
            std::string_view before = text.substr(0, index);
            before.remove_prefix(Utf8Text::lastCodepointStart(before));
            std::string_view after = text.substr(index + 1);
            uint32_t previous = 0;
            uint32_t next = 0;
            return Utf8Text::next(before, previous) && Utf8Text::next(after, next)
                && UnicodeText::isWordCharacter(previous) && UnicodeText::isWordCharacter(next);
        }

    } // namespace Detail

    bool hasReadableText(std::string_view text) {
        uint32_t codepoint = 0;
        while (Utf8Text::next(text, codepoint)) {
            if (UnicodeText::isWordCharacter(codepoint))
                return true;
        }
        return false;
    }

} // namespace RsvpText
