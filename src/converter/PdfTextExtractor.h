#pragma once

#include <expected>
#include <pdfio.h>
#include <cstddef>
#include <string>
#include <string_view>
#include <system_error>

class PdfTextExtractor {
public:
    struct Metadata {
        std::string title;
        std::string author;
    };

    using LineCallback = bool (*)(void* context, std::string_view line);

    static std::expected<PdfTextExtractor, std::error_code> open(std::string_view path);

    PdfTextExtractor(PdfTextExtractor&& other) noexcept;
    PdfTextExtractor& operator=(PdfTextExtractor&& other) noexcept;
    ~PdfTextExtractor();

    PdfTextExtractor(const PdfTextExtractor&) = delete;
    PdfTextExtractor& operator=(const PdfTextExtractor&) = delete;

    [[nodiscard]] const Metadata& metadata() const noexcept;
    [[nodiscard]] size_t pageCount() const noexcept;
    std::expected<void, std::error_code> extractPage(size_t pageIndex, LineCallback callback, void* context);

private:
    explicit PdfTextExtractor(pdfio_file_t* file);

    pdfio_file_t* file_ = nullptr;
    Metadata metadata_;
};
