#pragma once

#include <Arduino.h>

namespace RsvpText {

    struct ParseStats {
        size_t malformedUtf8 = 0;
        size_t nonAsciiCodepoints = 0;
        size_t longLineSplits = 0;
        bool memoryLow = false;
    };

    String normalizeDisplayText(const String& text, ParseStats* stats = nullptr);
    bool decodeMarkupEntity(const String& entity, String& decoded);
    String decodeMarkupEntities(const String& text);
    void trimAsciiWhitespace(String& text);

} // namespace RsvpText
