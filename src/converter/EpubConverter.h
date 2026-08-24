#pragma once

#include <expected>
#include <string_view>
#include <system_error>

#include "converter/ConversionOptions.h"

class EpubConverter {
public:
    struct Options {
        Conversion::Options conversion;
        size_t maxExtractBytes = 256UL * 1024UL;
        size_t maxContentBytes = 8UL * 1024UL * 1024UL;
    };

    inline static constexpr std::string_view kVersion = "stream-v11";

    static std::expected<void, std::error_code> convert(std::string_view epubPath, std::string_view rsvpPath,
                                                        const Options& options);
};
