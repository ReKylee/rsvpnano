#pragma once

#include <expected>
#include <string_view>
#include <system_error>

#include "conversion/ConversionOptions.h"

class PdfConverter {
public:
    using Options = Conversion::Options;

    inline static constexpr std::string_view kVersion = "pdfio-v2";

    static std::expected<void, std::error_code> convert(std::string_view pdfPath, std::string_view rsvpPath,
                                                        const Options& options);
};
