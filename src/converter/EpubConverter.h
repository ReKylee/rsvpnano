#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <system_error>

#ifndef RSVP_MAX_BOOK_WORDS
#define RSVP_MAX_BOOK_WORDS 0
#endif

class EpubConverter {
public:
    struct Options;

    using ProgressCallback = void (*)(const Options& options, const char* line1, const char* line2,
                                      int progressPercent);

    struct Options {
        Options() :
                maxWords(static_cast<size_t>(RSVP_MAX_BOOK_WORDS)),
                maxExtractBytes(256UL * 1024UL),
                maxContentBytes(8UL * 1024UL * 1024UL),
                progressCallback(nullptr) {}

        size_t maxWords;
        size_t maxExtractBytes;
        size_t maxContentBytes;
        ProgressCallback progressCallback;
        std::string progressTitle;
        std::string progressLabel;
    };

    static std::expected<void, std::error_code> convertIfNeeded(std::string_view epubPath, std::string_view rsvpPath,
                                                                const Options& options = Options());
    static bool isCurrentCache(std::string_view rsvpPath);
};
