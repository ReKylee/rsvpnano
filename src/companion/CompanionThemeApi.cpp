#include "companion/CompanionApi.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

#include "board/BoardStorage.h"
#include "companion/CompanionUpload.h"
#include "display/ThemeStore.h"
#include "storage/fs/StorageFiles.h"
#include "storage/fs/StoragePaths.h"

namespace {

    namespace api = companion::api;

    [[nodiscard]] api::HttpError themeInstallError(std::string error) {
        if (error.contains("already exists")) {
            return api::httpError(HTTP_CODE_CONFLICT, "already_exists", std::move(error), "file");
        }
        const t_http_codes status =
            error.contains("could not") ? HTTP_CODE_INTERNAL_SERVER_ERROR : HTTP_CODE_UNPROCESSABLE_ENTITY;
        return api::httpError(status, status == HTTP_CODE_INTERNAL_SERVER_ERROR ? "storage_error" : "invalid_theme",
                              std::move(error), "file");
    }

} // namespace

companion::api::Result<std::span<const ui::themes::Theme>> CompanionApi::getThemes(httpd_req_t& request) {
    (void) request;
    return interfaceScreen_.themes.themes();
}

companion::api::Result<companion::api::Located<ui::themes::Theme>> CompanionApi::postTheme(httpd_req_t& request) {
    auto requestedName = requiredQueryParameter(request, "name", "Theme filename is required");
    if (!requestedName)
        return std::unexpected(companion::api::closeConnection(std::move(requestedName.error())));

    std::string filename = StoragePaths::sanitizeFilename(*requestedName);
    if (filename.empty()) {
        return std::unexpected(companion::api::httpError(HTTP_CODE_BAD_REQUEST, "invalid_field",
                                                         "Theme filename is invalid", "name",
                                                         companion::api::ConnectionPolicy::Close));
    }
    if (!ui::themes::hasThemeExtension(filename))
        filename += ui::themes::kThemeExtension;

    if (auto created = StorageFiles::ensureDirectory(StoragePaths::kThemesPath); !created) {
        return std::unexpected(companion::api::httpError(HTTP_CODE_INTERNAL_SERVER_ERROR, "storage_error",
                                                         "Themes folder unavailable: " + created.error().message(),
                                                         std::nullopt, companion::api::ConnectionPolicy::Close));
    }

    const std::string finalPath = std::string{StoragePaths::kThemesPath} + "/" + filename;
    auto upload = companion::TemporaryUpload::receive(request, Board::Storage::filesystem(), finalPath + ".tmp",
                                                      ThemeStore::kMaximumFileBytes, "Theme");
    if (!upload)
        return std::unexpected(std::move(upload.error()));

    auto installed = interfaceScreen_.themes.install(upload->path(), finalPath);
    if (!installed)
        return std::unexpected(themeInstallError(std::move(installed.error())));

    const ui::themes::Theme& theme = installed->get();
    ui_.setTheme(interfaceScreen_.themes.resolve(settingsStore_.settings().interface.selectedThemeId));
    return companion::api::Located<ui::themes::Theme>{
        .location = "/api/v2/themes/" + theme.id,
        .value = std::cref(theme),
    };
}

companion::api::Result<> CompanionApi::deleteTheme(httpd_req_t& request) {
    auto id = routeId(request, "/api/v2/themes/");
    if (!id)
        return std::unexpected(std::move(id.error()));

    const auto& themes = interfaceScreen_.themes.themes();
    const auto theme = std::ranges::find(themes, *id, &ui::themes::Theme::id);
    if (theme == themes.end()) {
        return std::unexpected(companion::api::httpError(HTTP_CODE_NOT_FOUND, "theme_not_found", "Theme not found",
                                                         "id"));
    }
    if (theme->builtIn) {
        return std::unexpected(companion::api::httpError(HTTP_CODE_UNPROCESSABLE_ENTITY, "builtin_theme",
                                                         "The built-in theme cannot be removed", "id"));
    }
    if (settingsStore_.settings().interface.selectedThemeId == theme->id) {
        return std::unexpected(companion::api::httpError(HTTP_CODE_CONFLICT, "resource_in_use",
                                                         "Select another theme before removing this one", "id"));
    }

    const std::string themeId = theme->id;
    return interfaceScreen_.themes.remove(themeId)
        .transform_error([](std::string error) {
            return companion::api::httpError(HTTP_CODE_INTERNAL_SERVER_ERROR, "storage_error", std::move(error));
        })
        .transform([this] {
            ui_.setTheme(interfaceScreen_.themes.resolve(settingsStore_.settings().interface.selectedThemeId));
        });
}
