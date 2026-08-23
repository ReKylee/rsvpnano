#include "companion/http/CompanionApi.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

#include "board/BoardStorage.h"
#include "companion/http/CompanionUpload.h"
#include "fonts/FontCatalog.h"
#include "storage/fs/StorageFiles.h"
#include "storage/fs/StoragePaths.h"

namespace {

    namespace api = companion::api;
    constexpr size_t kMaxFontUploadBytes = 96UL * 1024UL * 1024UL;

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

companion::api::Result<std::span<const FontCatalog::Family>> CompanionApi::getFonts(httpd_req_t& request) {
    (void) request;
    return readerScreen_.fonts.families();
}

companion::api::Result<companion::api::Located<FontCatalog::Family>> CompanionApi::postFont(httpd_req_t& request) {
    if (auto created = StorageFiles::ensureDirectory(StoragePaths::kFontsPath); !created) {
        return std::unexpected(companion::api::httpError(HTTP_CODE_INTERNAL_SERVER_ERROR, "storage_error",
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

    const FontCatalog::Family& family = installed->get();
    readerScreen_.releaseRuntimeCaches();
    return companion::api::Located<FontCatalog::Family>{
        .location = "/api/v2/fonts/" + family.id,
        .value = std::cref(family),
    };
}

companion::api::Result<> CompanionApi::deleteFont(httpd_req_t& request) {
    auto id = routeId(request, "/api/v2/fonts/");
    if (!id)
        return std::unexpected(std::move(id.error()));

    const auto family = readerScreen_.fonts.find(*id);
    if (!family) {
        return std::unexpected(companion::api::httpError(HTTP_CODE_NOT_FOUND, "font_not_found", "Font not found",
                                                         "id"));
    }
    if (family->builtIn) {
        return std::unexpected(companion::api::httpError(HTTP_CODE_UNPROCESSABLE_ENTITY, "builtin_font",
                                                         "The built-in font cannot be removed", "id"));
    }

    const std::string fontId = family->id;
    if (settingsStore_.settings().reading.typography.fontId == fontId) {
        return std::unexpected(companion::api::httpError(HTTP_CODE_CONFLICT, "resource_in_use",
                                                         "Select another font before removing this one", "id"));
    }

    if (auto removed = readerScreen_.fonts.remove(fontId); !removed) {
        return std::unexpected(companion::api::httpError(HTTP_CODE_INTERNAL_SERVER_ERROR, "storage_error",
                                                         std::move(removed.error())));
    }
    readerScreen_.releaseRuntimeCaches();
    return {};
}
