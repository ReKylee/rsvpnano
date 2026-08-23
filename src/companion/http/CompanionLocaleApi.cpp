#include "companion/http/CompanionApi.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

#include "board/BoardStorage.h"
#include "companion/http/CompanionUpload.h"
#include "storage/fs/StorageFiles.h"
#include "storage/fs/StoragePaths.h"
#include "ui/Localization.h"

namespace {

    namespace api = companion::api;
    constexpr size_t kMaxLocalePackUploadBytes = 256UL * 1024UL;

    [[nodiscard]] api::HttpError localeInstallError(std::string error) {
        const bool alreadyExists = error.contains("already exists") || error.contains("another pack");
        if (alreadyExists)
            return api::httpError(HTTP_CODE_CONFLICT, "already_exists", std::move(error), "file");

        const t_http_codes status =
            error.contains("could not") ? HTTP_CODE_INTERNAL_SERVER_ERROR : HTTP_CODE_UNPROCESSABLE_ENTITY;
        return api::httpError(status,
                              status == HTTP_CODE_INTERNAL_SERVER_ERROR ? "storage_error" : "invalid_locale_pack",
                              std::move(error), "file");
    }

} // namespace

companion::api::Result<std::span<const locales::InstalledPack>> CompanionApi::getLocales(httpd_req_t& request) {
    (void) request;
    return std::span<const locales::InstalledPack>{localeCatalog_};
}

companion::api::Result<companion::api::Located<locales::InstalledPack>> CompanionApi::postLocale(httpd_req_t& request) {
    if (auto created = StorageFiles::ensureDirectory(StoragePaths::kLocalesPath); !created) {
        return std::unexpected(companion::api::httpError(HTTP_CODE_INTERNAL_SERVER_ERROR, "storage_error",
                                                         "Locale-pack folder is unavailable: "
                                                             + created.error().message(),
                                                         std::nullopt, companion::api::ConnectionPolicy::Close));
    }

    const std::string temporaryPath = std::string{StoragePaths::kLocalesPath} + "/.upload.zip";
    auto upload = companion::TemporaryUpload::receive(request, Board::Storage::filesystem(), temporaryPath,
                                                      kMaxLocalePackUploadBytes, "Locale pack");
    if (!upload)
        return std::unexpected(std::move(upload.error()));

    auto installed = locales::installArchive(Board::Storage::filesystem(), localeCatalog_, upload->path(),
                                              static_cast<size_t>(UiText::Count));
    if (!installed)
        return std::unexpected(localeInstallError(std::move(installed.error())));

    const locales::InstalledPack& pack = installed->get();

    const std::string& selectedLocale = settingsStore_.settings().interface.locale;
    auto assets = locales::loadUiAssets(Board::Storage::filesystem(), localeCatalog_, selectedLocale,
                                        static_cast<size_t>(UiText::Count));
    if (assets) {
        ui_.setLanguageAssets(std::move(*assets));
    } else {
        ESP_LOGW("companion", "could not reload locale %s: %s", selectedLocale.c_str(), assets.error().c_str());
    }

    return companion::api::Located<locales::InstalledPack>{
        .location = "/api/v2/locales/" + pack.id,
        .value = std::cref(pack),
    };
}

companion::api::Result<> CompanionApi::deleteLocale(httpd_req_t& request) {
    auto id = routeId(request, "/api/v2/locales/");
    if (!id)
        return std::unexpected(std::move(id.error()));

    const auto pack =
        std::ranges::find(localeCatalog_, std::string_view{*id}, [](const locales::InstalledPack& installed) {
            return std::string_view{installed.id};
        });
    if (pack == localeCatalog_.end()) {
        return std::unexpected(companion::api::httpError(HTTP_CODE_NOT_FOUND, "locale_not_found",
                                                         "Locale pack not found", "id"));
    }
    if (settingsStore_.settings().interface.locale == pack->locale) {
        return std::unexpected(companion::api::httpError(HTTP_CODE_CONFLICT, "resource_in_use",
                                                         "Select another interface language before removing this pack",
                                                         "id"));
    }

    return locales::removeInstalled(Board::Storage::filesystem(), localeCatalog_, *id)
        .transform_error([](std::string error) {
            const t_http_codes status =
                error == "invalid pack ID" ? HTTP_CODE_BAD_REQUEST : HTTP_CODE_INTERNAL_SERVER_ERROR;
            return companion::api::httpError(status, "remove_failed", std::move(error), "id");
        })
        .transform([this] {
            ui_.setLanguageCatalog(&Board::Storage::filesystem(), &localeCatalog_, &locales::loadUiFont);
        });
}
