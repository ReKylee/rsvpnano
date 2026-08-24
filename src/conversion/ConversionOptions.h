#pragma once

#include <cstddef>

#ifndef RSVP_MAX_BOOK_WORDS
#define RSVP_MAX_BOOK_WORDS 0
#endif

namespace Conversion {

    using ProgressCallback = void (*)(void* context, const char* line1, const char* line2, int progressPercent);

    struct Options {
        size_t maxWords = static_cast<size_t>(RSVP_MAX_BOOK_WORDS);
        ProgressCallback progressCallback = nullptr;
        void* progressContext = nullptr;
    };

} // namespace Conversion
