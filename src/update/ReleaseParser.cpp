#include "update/ReleaseParser.h"

#include <algorithm>
#include <cctype>
#include <vector>

#include <glaze/json.hpp>

#include "text/AsciiText.h"

namespace releaseparser {
    namespace {

        struct Asset {
            std::string name;
            std::string browser_download_url;
        };

        struct Release {
            std::string tag_name;
            std::vector<Asset> assets;
        };

    } // namespace

    std::expected<ReleaseInfo, std::error_code> parse(std::string_view json, std::string_view assetName) {
        Release release;
        if (glz::read<glz::opts{.error_on_unknown_keys = false}>(release, json) || release.tag_name.empty()) {
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        }

        const auto asset = std::ranges::find(release.assets, assetName, &Asset::name);
        return ReleaseInfo{
            .tagName = std::move(release.tag_name),
            .assetUrl = asset == release.assets.end() ? std::string{} : std::move(asset->browser_download_url),
        };
    }

    std::expected<std::string, std::error_code> versionForCommit(std::string_view tagName, std::string_view commitSha) {
        commitSha = AsciiText::trim(commitSha);
        if (tagName.empty() || commitSha.length() != 40) {
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        }
        if (!std::ranges::all_of(commitSha, [](char c) {
                return std::isxdigit(static_cast<unsigned char>(c));
            })) {
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        }

        std::string version;
        version.reserve(tagName.size() + 13);
        version.append(tagName).append("+").append(commitSha.substr(0, 12));
        return version;
    }

} // namespace releaseparser
