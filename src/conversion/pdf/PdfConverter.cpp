#include "conversion/pdf/PdfConverter.h"

#include <esp_log.h>

#include <algorithm>
#include <string>

#include "board/BoardStorage.h"
#include "conversion/pdf/PdfTextExtractor.h"
#include "conversion/rsvp/RsvpWriter.h"
#include "storage/fs/StoragePaths.h"
#include "text/TextNormalizer.h"

namespace {

    struct OutputContext {
        RsvpWriter& writer;
        bool stoppedAtLimit = false;
    };

    void reportProgress(const PdfConverter::Options& options, const char* line1, const char* line2,
                        int progressPercent) {
        if (options.progressCallback != nullptr)
            options.progressCallback(options.progressContext, line1, line2, std::clamp(progressPercent, 0, 100));
        yield();
        delay(0);
    }

    std::string basenameWithoutExtension(std::string_view path) {
        std::string name = StoragePaths::displayNameForPath(path);
        if (StoragePaths::hasPdfExtension(name))
            name.resize(name.size() - std::string_view{StoragePaths::kPdfExtension}.size());
        return RsvpText::normalizeDisplayText(name);
    }

    std::string vfsPath(std::string_view storagePath) {
        if (storagePath.starts_with(StoragePaths::kMountPoint))
            return std::string{storagePath};
        std::string path{StoragePaths::kMountPoint};
        path += storagePath;
        return path;
    }

    bool writeExtractedLine(void* context, std::string_view line) {
        auto& output = *static_cast<OutputContext*>(context);
        const bool withinLimit = output.writer.writeText(line, false);
        output.stoppedAtLimit = !withinLimit;
        return withinLimit;
    }

    std::expected<void, std::error_code> convertPdf(std::string_view pdfPath, std::string_view rsvpPath,
                                                    const PdfConverter::Options& options) {
        reportProgress(options, "Reading PDF", "Opening document", 2);
        auto extractor = PdfTextExtractor::open(vfsPath(pdfPath));
        if (!extractor)
            return std::unexpected(extractor.error());

        const std::string title = extractor->metadata().title.empty() ? basenameWithoutExtension(pdfPath)
                                                                      : extractor->metadata().title;
        const std::string destination{rsvpPath};
        Board::Storage::filesystem().remove(destination.c_str());
        File output = Board::Storage::filesystem().open(destination.c_str(), FILE_WRITE);
        if (!output)
            return std::unexpected(std::make_error_code(std::errc::io_error));

        RsvpWriter writer(output,
                          {.source = pdfPath,
                           .title = title,
                           .author = extractor->metadata().author,
                           .converter = PdfConverter::kVersion},
                          options.maxWords);
        writer.writeChapter(title);

        OutputContext context{.writer = writer};
        const size_t pageCount = extractor->pageCount();
        for (size_t page = 0; page < pageCount && !context.stoppedAtLimit; ++page) {
            writer.endParagraph();
            const std::string detail = "Page " + std::to_string(page + 1) + " of " + std::to_string(pageCount);
            const int progress = pageCount == 0 ? 90 : 10 + static_cast<int>((80 * page) / pageCount);
            reportProgress(options, "Extracting text", detail.c_str(), progress);
            const auto extracted = extractor->extractPage(page, writeExtractedLine, &context);
            if (!extracted && !context.stoppedAtLimit) {
                output.close();
                Board::Storage::filesystem().remove(destination.c_str());
                return std::unexpected(extracted.error());
            }
        }
        const bool flushed = writer.finish();
        output.close();

        if (!flushed || writer.wordCount() == 0) {
            Board::Storage::filesystem().remove(destination.c_str());
            return std::unexpected(std::make_error_code(std::errc::no_message));
        }

        const std::string detail = std::to_string(writer.wordCount()) + " words";
        reportProgress(options, "PDF converted", detail.c_str(), 100);
        ESP_LOGI("pdf", "Converted %.*s to RSVP (%u words)", static_cast<int>(pdfPath.size()), pdfPath.data(),
                 static_cast<unsigned>(writer.wordCount()));
        return {};
    }

} // namespace

std::expected<void, std::error_code> PdfConverter::convert(std::string_view pdfPath, std::string_view rsvpPath,
                                                           const Options& options) {
    return convertPdf(pdfPath, rsvpPath, options);
}
