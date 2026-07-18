#pragma once

#include <Arduino.h>
#include <expected>
#include <system_error>

// Pure parsing of a GitHub release JSON payload. No networking, no
// SD access -- safe to unit test on the host with the Arduino String shim.
namespace releaseparser {

    struct ReleaseInfo {
        String tagName; // empty if the payload had no usable tag_name
        String assetUrl; // empty if no asset matched assetName
    };

    // Extracts the release tag and the browser_download_url of the matching asset.
    std::expected<ReleaseInfo, std::error_code> parse(const String& json, const String& assetName);

    // Published builds use the release tag plus a stable abbreviated commit.
    std::expected<String, std::error_code> versionForCommit(const String& tagName, String commitSha);

} // namespace releaseparser
