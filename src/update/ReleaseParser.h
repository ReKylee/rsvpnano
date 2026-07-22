#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <system_error>

// Pure parsing of a GitHub release JSON payload. No networking, no
// SD access -- safe to unit test on the host.
namespace releaseparser {

    struct ReleaseInfo {
        std::string tagName; // empty if the payload had no usable tag_name
        std::string assetUrl; // empty if no asset matched assetName
    };

    // Extracts the release tag and the browser_download_url of the matching asset.
    std::expected<ReleaseInfo, std::error_code> parse(std::string_view json, std::string_view assetName);

    // Published builds use the release tag plus a stable abbreviated commit.
    std::expected<std::string, std::error_code> versionForCommit(std::string_view tagName, std::string_view commitSha);

} // namespace releaseparser
