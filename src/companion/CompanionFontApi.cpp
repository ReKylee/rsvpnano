#include "companion/CompanionApi.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "board/BoardStorage.h"
#include "companion/CompanionUpload.h"
#include "fonts/FontCatalog.h"
#include "storage/fs/StorageFiles.h"
#include "storage/fs/StoragePaths.h"
#include "text/UnicodeText.h"

namespace {

    namespace api = companion::api;
    constexpr size_t kMaxFontUploadBytes = 96UL * 1024UL * 1024UL;

    [[nodiscard]] api::FontSummary summarizeFont(const FontCatalog::Family& family) {
        api::FontSummary summary{
            .id = family.id,
            .name = family.label,
            .locales = {},
            .scripts = UnicodeText::scriptTags(family.scriptMask),
            .builtIn = family.builtIn,
        };
        for (size_t offset = 0; offset < family.locales.size();) {
            const std::string_view locale{family.locales.data() + offset};
            summary.locales.emplace_back(locale);
            offset += locale.size() + 1;
        }
        return summary;
    }

    [[nodiscard]] api::HttpError fontInstallError(std::string error) {
        if (error.contains("already exists")) {
            return api::httpError(HTTP_CODE_CONFLICT, "already_exists", std::move(error), "file");
        }
        const t_http_codes status =
            error.contains("could not") ? HTTP_CODE_INTERNAL_SERVER_ERROR : HTTP_CODE_UNPROCESSABLE_ENTITY;
        return api::httpError(status, status == HTTP_CODE_INTERNAL_SERVER_ERROR ? "storage_error" : "invalid_font",
                              std::move(error), "file");
    }

} // namespace

companion::api::Result<std::vector<companion::api::FontSummary>> CompanionApi::getFonts(httpd_req_t& request) {
    (void) request;
    std::vector<companion::api::FontSummary> fonts;
    fonts.reserve(readerScreen_.fonts.families().size());
    std::ranges::transform(readerScreen_.fonts.families(), std::back_inserter(fonts), summarizeFont);
    return fonts;
}

companion::api::Result<companion::api::Located<companion::api::FontSummary>> CompanionApi::postFont(httpd_req_t&
                                                                                                        request) {
    if (auto created = StorageFiles::ensureDirectory(StoragePaths::kFontsPath); !created) {
        return std::unexpected(companion::api::httpError(HTTP_CODE_INTERNAL_SERVER_ERROR,
                                                         "storage_error",
                                                         "Fonts folder unavailable: " + created.error().message(),
                                                         std::nullopt, companion::api::ConnectionPolicy::Close));
    }

    const std::string temporaryPath = std::string{StoragePaths::kFontsPath} + "/.upload.rfont4.tmp";
    auto upload = companion::TemporaryUpload::receive(request, Board::Storage::filesystem(), temporaryPath,
                                                      kMaxFontUploadBytes, "Font");
    if (!upload)
        return std::unexpected(std::move(upload.error()));

    auto installed = readerScreen_.fonts.install(upload->path());
    if (!installed)
        return std::unexpected(fontInstallError(std::move(installed.error())));

    const FontCatalog::Family& family = *installed;
    readerScreen_.refreshTypography(settingsStore_.settings().reading, readerScreen_.session.state.overrides);
    return companion::api::Located<companion::api::FontSummary>{
        .location = "/api/v2/fonts/" + family.id,
        .value = summarizeFont(family),
    };
}

companion::api::Result<> CompanionApi::deleteFont(httpd_req_t& request) {
    auto id = routeId(request, "/api/v2/fonts/");
    if (!id)
        return std::unexpected(std::move(id.error()));

    const auto family = readerScreen_.fonts.find(*id);
    if (!family) {
        return std::unexpected(companion::api::httpError(HTTP_CODE_NOT_FOUND, "font_not_found",
                                                         "Font not found", "id"));
    }
    if (family->get().builtIn) {
        return std::unexpected(companion::api::httpError(HTTP_CODE_UNPROCESSABLE_ENTITY,
                                                         "builtin_font", "The built-in font cannot be removed", "id"));
    }

    const std::string fontId = family->get().id;
    if (settingsStore_.settings().reading.typography.fontId == fontId) {
        return std::unexpected(companion::api::httpError(HTTP_CODE_CONFLICT, "resource_in_use",
                                                         "Select another font before removing this one", "id"));
    }

    if (auto removed = readerScreen_.fonts.remove(fontId); !removed) {
        return std::unexpected(companion::api::httpError(HTTP_CODE_INTERNAL_SERVER_ERROR,
                                                         "storage_error", std::move(removed.error())));
    }
    readerScreen_.refreshTypography(settingsStore_.settings().reading, readerScreen_.session.state.overrides);
    return {};
}
