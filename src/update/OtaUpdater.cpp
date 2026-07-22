#include "update/OtaUpdater.h"

#include <algorithm>
#include <expected>
#include <string>
#include <utility>

#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "FirmwareVersion.generated.h"
#include "net/WifiConnection.h"
#include "text/AsciiText.h"
#include "update/ReleaseParser.h"

namespace {

    constexpr size_t kMaxReleaseJsonBytes = 32768;
    constexpr const char* kStatusTitle = "OTA";
    const char* kRedirectHeaderKeys[] = {
        "Location",
    };

    struct ReleaseSource {
        std::string owner;
        std::string repo;
        std::string tag;
    };

    struct LatestRelease {
        std::string version;
        std::string assetUrl;
    };

    struct Error {
        OtaUpdater::ResultCode code;
        std::string summary;
        std::string detail;
    };

    std::string trimCopy(std::string_view value) {
        return std::string{AsciiText::trim(value)};
    }

    bool isUrlUnreserved(char value) {
        return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9')
            || value == '-' || value == '.' || value == '_' || value == '~';
    }

    std::string urlEncodePathSegment(std::string_view value) {
        constexpr char kHex[] = "0123456789ABCDEF";
        std::string encoded;
        encoded.reserve(value.length());
        for (const char c: value) {
            if (isUrlUnreserved(c)) {
                encoded += c;
                continue;
            }

            const uint8_t byte = static_cast<uint8_t>(c);
            encoded += '%';
            encoded += kHex[byte >> 4];
            encoded += kHex[byte & 0x0F];
        }
        return encoded;
    }

    bool splitOwnerRepo(std::string_view value, std::string& owner, std::string& repo) {
        const std::string_view trimmed = AsciiText::trim(value);
        const size_t slash = trimmed.find('/');
        if (slash == 0 || slash == std::string_view::npos || slash + 1 >= trimmed.length()) {
            return false;
        }

        owner = AsciiText::trim(trimmed.substr(0, slash));
        repo = AsciiText::trim(trimmed.substr(slash + 1));
        return !owner.empty() && !repo.empty();
    }

    ReleaseSource releaseSourceForConfig(const OtaUpdater::Config& config) {
        ReleaseSource source{trimCopy(config.githubOwner), trimCopy(config.githubRepo), trimCopy(config.githubTag)};

        splitOwnerRepo(source.owner, source.owner, source.repo);
        splitOwnerRepo(source.repo, source.owner, source.repo);

        const size_t at = source.tag.find('@');
        if (at > 0 && at + 1 < source.tag.length()) {
            std::string repoPart = trimCopy(std::string_view{source.tag}.substr(0, at));
            source.tag = trimCopy(std::string_view{source.tag}.substr(at + 1));
            if (!splitOwnerRepo(repoPart, source.owner, source.repo) && !repoPart.empty()) {
                source.repo = repoPart;
            }
        }

        return source;
    }

    std::string httpClientErrorDetail(std::string_view prefix, int statusCode) {
        if (statusCode >= 0) {
            return std::string{prefix} + " HTTP " + std::to_string(statusCode);
        }

        const String detail = HTTPClient::errorToString(statusCode);
        return std::string{prefix} + " " + std::string{detail.c_str(), detail.length()};
    }

    std::string readBodyLimited(HTTPClient& http, size_t maxBytes) {
        WiFiClient* stream = http.getStreamPtr();
        if (stream == nullptr) {
            return {};
        }

        const int reportedSize = http.getSize();
        std::string body;
        const size_t reserveBytes = reportedSize > 0 ? std::min(static_cast<size_t>(reportedSize), maxBytes) : 1024;
        body.reserve(reserveBytes);

        uint8_t buffer[512];
        size_t totalRead = 0;
        while (http.connected() || stream->available()) {
            if (reportedSize > 0 && totalRead >= static_cast<size_t>(reportedSize)) {
                break;
            }

            const int available = stream->available();
            if (available <= 0) {
                delay(1);
                continue;
            }

            const size_t remaining = maxBytes - totalRead;
            if (remaining == 0) {
                break;
            }

            const size_t chunkSize = std::min(remaining, std::min(sizeof(buffer), static_cast<size_t>(available)));
            const int bytesRead = stream->readBytes(buffer, chunkSize);
            if (bytesRead <= 0) {
                break;
            }

            totalRead += static_cast<size_t>(bytesRead);
            body.append(reinterpret_cast<const char*>(buffer), static_cast<size_t>(bytesRead));
        }

        return body;
    }

    std::string userAgentForVersion(std::string_view version) {
        return "RSVP-Nano/" + std::string{version.empty() ? std::string_view{"dev"} : version};
    }

    std::string versionDetail(std::string_view currentVersion, std::string_view latestVersion) {
        if (latestVersion.empty()) {
            return std::string{currentVersion};
        }
        if (currentVersion.empty()) {
            return std::string{latestVersion};
        }
        return std::string{currentVersion} + " -> " + std::string{latestVersion};
    }

    std::expected<void, std::string> validateAssetName(std::string_view assetName) {
        assetName = AsciiText::trim(assetName);
        if (assetName.empty())
            return std::unexpected(std::string{"Asset name missing"});

        if (assetName != Board::Config::OTA_ASSET_NAME)
            return std::unexpected(std::string{"Asset does not match "} + Board::Config::BOARD_LABEL);

        return {};
    }

} // namespace

OtaUpdater::Config OtaUpdater::config(const settings::DeviceSettings& settings,
                                      const settings::DeviceSecrets& secrets) {
    Config result;
    if (!settings.network.wifiSsid.empty()) {
        result.wifiSsid = settings.network.wifiSsid;
        result.wifiPassword = secrets.wifiPassword;
    }

    if (!settings.updates.repositoryOwner.empty())
        result.githubOwner = settings.updates.repositoryOwner;
    result.githubTag = settings.updates.releaseTag;
    return result;
}

std::string_view OtaUpdater::currentVersion() {
    return kFirmwareVersion;
}

static void reportStatus(OtaUpdater::StatusCallback callback, void* context, const char* title, const char* line1,
                         const char* line2, int progressPercent);

static std::expected<LatestRelease, std::string> fetchRelease(const OtaUpdater::Config& config,
                                                              OtaUpdater::StatusCallback callback, void* context) {
    const std::string installedVersion{OtaUpdater::currentVersion()};
    const ReleaseSource source = releaseSourceForConfig(config);
    if (source.owner.empty() || source.repo.empty())
        return std::unexpected(std::string{"GitHub source missing"});

    const std::string releasePath = source.tag.empty() ? "latest" : "tags/" + urlEncodePathSegment(source.tag);
    const std::string url =
        "https://api.github.com/repos/" + source.owner + "/" + source.repo + "/releases/" + releasePath;
    const std::string sourceLabel = source.tag.empty() ? source.repo : source.repo + ":" + source.tag;

    reportStatus(callback, context, kStatusTitle, "Checking GitHub", sourceLabel.c_str(), 22);

    WiFiClientSecure client;
    // GitHub release metadata and assets can redirect across multiple hosts, so
    // keep the transport flexible for now. A signed manifest is the best
    // follow-up hardening step.
    client.setInsecure();
    client.setHandshakeTimeout(15);

    HTTPClient http;
    http.setUserAgent(userAgentForVersion(installedVersion).c_str());
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(15000);
    if (!http.begin(client, url.c_str()))
        return std::unexpected(std::string{"HTTP begin failed"});

    http.addHeader("Accept", "application/vnd.github+json");
    const int statusCode = http.GET();
    if (statusCode != HTTP_CODE_OK) {
        const std::string errorDetail = statusCode == HTTP_CODE_NOT_FOUND
                                          ? (source.tag.empty() ? "No published release" : "Release tag not found")
                                          : httpClientErrorDetail("GitHub", statusCode);
        http.end();
        return std::unexpected(errorDetail);
    }

    const std::string body = readBodyLimited(http, kMaxReleaseJsonBytes);
    http.end();

    auto parsed = releaseparser::parse(body, config.assetName);
    if (!parsed)
        return std::unexpected(std::string{"Release tag missing"});
    LatestRelease release;
    release.assetUrl = parsed->assetUrl;

    reportStatus(callback, context, kStatusTitle, "Checking version", parsed->tagName.c_str(), 25);
    const std::string commitUrl = "https://api.github.com/repos/" + source.owner + "/" + source.repo + "/commits/"
                                + urlEncodePathSegment(parsed->tagName);
    if (!http.begin(client, commitUrl.c_str()))
        return std::unexpected(std::string{"Commit lookup failed"});
    http.addHeader("Accept", "application/vnd.github.sha");
    const int commitStatus = http.GET();
    if (commitStatus != HTTP_CODE_OK) {
        const std::string errorDetail = httpClientErrorDetail("Tag commit", commitStatus);
        http.end();
        return std::unexpected(errorDetail);
    }
    std::string commitSha = readBodyLimited(http, 64);
    http.end();
    auto releaseVersion = releaseparser::versionForCommit(parsed->tagName, commitSha);
    if (!releaseVersion)
        return std::unexpected(std::string{"Tag commit invalid"});
    release.version = std::move(*releaseVersion);

    if (release.assetUrl.empty())
        return std::unexpected(config.assetName + " missing");

    return release;
}

static std::expected<std::string, std::string> resolveDownloadUrl(std::string_view assetUrl, std::string_view version,
                                                                  OtaUpdater::StatusCallback callback, void* context) {
    const std::string assetUrlString{assetUrl};
    const std::string versionString{version};
    reportStatus(callback, context, kStatusTitle, "Resolving asset", versionString.c_str(), 29);

    WiFiClientSecure client;
    client.setInsecure();
    client.setHandshakeTimeout(15);

    HTTPClient http;
    http.collectHeaders(kRedirectHeaderKeys, 1);
    http.setUserAgent(userAgentForVersion(versionString).c_str());
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.setTimeout(15000);
    if (!http.begin(client, assetUrlString.c_str()))
        return std::unexpected(std::string{"Asset URL failed"});

    http.addHeader("Accept", "application/octet-stream");
    const int statusCode = http.GET();
    if (statusCode == HTTP_CODE_OK) {
        http.end();
        return std::string{assetUrl};
    }

    if (statusCode == HTTP_CODE_MOVED_PERMANENTLY || statusCode == HTTP_CODE_FOUND || statusCode == HTTP_CODE_SEE_OTHER
        || statusCode == HTTP_CODE_TEMPORARY_REDIRECT || statusCode == HTTP_CODE_PERMANENT_REDIRECT) {
        String resolvedUrl = http.header("Location");
        http.end();
        if (!resolvedUrl.isEmpty())
            return std::string{resolvedUrl.c_str(), resolvedUrl.length()};
        return std::unexpected(std::string{"Asset redirect missing"});
    }

    const std::string errorDetail = httpClientErrorDetail("Asset", statusCode);
    http.end();
    return std::unexpected(errorDetail);
}

static void reportStatus(OtaUpdater::StatusCallback callback, void* context, const char* title, const char* line1,
                         const char* line2, int progressPercent) {
    if (callback == nullptr) {
        return;
    }

    callback(context, title, line1, line2, progressPercent);
}

static OtaUpdater::Result resultForError(Error error) {
    return {
        .code = error.code,
        .currentVersion = std::string{OtaUpdater::currentVersion()},
        .summary = std::move(error.summary),
        .detail = std::move(error.detail),
    };
}

static std::expected<LatestRelease, Error> prepareRelease(const OtaUpdater::Config& config,
                                                          OtaUpdater::StatusCallback callback, void* context) {
    if (auto compatible = validateAssetName(config.assetName); !compatible) {
        return std::unexpected(Error{
            .code = OtaUpdater::ResultCode::AssetMismatch,
            .summary = "Wrong OTA asset",
            .detail = std::move(compatible.error()),
        });
    }

    if (AsciiText::trim(config.wifiSsid).empty()) {
        return std::unexpected(Error{
            .code = OtaUpdater::ResultCode::NotConfigured,
            .summary = "Wi-Fi not set",
            .detail = "Settings -> Wi-Fi",
        });
    }

    auto connected = net::connectStation(config.wifiSsid.c_str(), config.wifiPassword.c_str(), [&](int percent) {
        reportStatus(callback, context, kStatusTitle, "Connecting Wi-Fi", config.wifiSsid.c_str(), percent);
    });
    if (!connected) {
        net::disconnect();
        return std::unexpected(Error{
            .code = OtaUpdater::ResultCode::ConnectFailed,
            .summary = "Wi-Fi failed",
            .detail = connected.error().message(),
        });
    }

    auto release = fetchRelease(config, callback, context);
    if (!release) {
        net::disconnect();
        return std::unexpected(Error{
            .code = OtaUpdater::ResultCode::MetadataFailed,
            .summary = "GitHub failed",
            .detail = std::move(release.error()),
        });
    }

    return std::move(*release);
}

OtaUpdater::Result OtaUpdater::checkOnly(const Config& config, StatusCallback callback, void* context) {
    auto release = prepareRelease(config, callback, context);
    if (!release)
        return resultForError(std::move(release.error()));

    net::disconnect();
    Result result;
    result.currentVersion = currentVersion();
    result.latestVersion = release->version;
    if (release->version == result.currentVersion) {
        result.code = ResultCode::NoUpdate;
        result.summary = "Already current";
        result.detail = release->version;
        return result;
    }

    result.code = ResultCode::UpdateAvailable;
    result.summary = "Update available";
    result.detail = release->version;
    return result;
}

OtaUpdater::Result OtaUpdater::checkAndInstall(const Config& config, StatusCallback callback, void* context) {
    auto release = prepareRelease(config, callback, context);
    if (!release)
        return resultForError(std::move(release.error()));

    Result result;
    result.currentVersion = currentVersion();
    result.latestVersion = release->version;
    if (release->version == result.currentVersion) {
        net::disconnect();
        result.code = ResultCode::NoUpdate;
        result.summary = "Already current";
        result.detail = release->version;
        return result;
    }

    const std::string detail = versionDetail(result.currentVersion, result.latestVersion);
    reportStatus(callback, context, kStatusTitle, "Preparing update", detail.c_str(), 28);

    auto resolvedAssetUrl = resolveDownloadUrl(release->assetUrl, result.latestVersion, callback, context);
    if (!resolvedAssetUrl) {
        net::disconnect();
        result.code = ResultCode::InstallFailed;
        result.summary = "Asset failed";
        result.detail = resolvedAssetUrl.error();
        return result;
    }

    WiFiClientSecure client;
    // Match the metadata request behavior until the update path gains certificate
    // pinning or signature verification above the transport layer.
    client.setInsecure();
    client.setHandshakeTimeout(15);

    HTTPUpdate updater;
    updater.rebootOnUpdate(false);
    updater.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    int lastReportedProgress = -1;
    updater.onProgress([callback, context, &result, &lastReportedProgress](int current, int total) {
        if (total <= 0) {
            reportStatus(callback, context, kStatusTitle, "Downloading update", result.latestVersion.c_str(), -1);
            return;
        }

        const int progress = 30 + static_cast<int>((static_cast<int64_t>(current) * 65) / total);
        if (progress == lastReportedProgress) {
            return;
        }

        lastReportedProgress = progress;
        reportStatus(callback, context, kStatusTitle, "Downloading update", result.latestVersion.c_str(), progress);
    });

    const String version = result.currentVersion.c_str();
    const String resolvedUrl = resolvedAssetUrl->c_str();
    const t_httpUpdate_return updateResult = updater.update(client, resolvedUrl, version, [version](HTTPClient* http) {
        http->setUserAgent(userAgentForVersion({version.c_str(), version.length()}).c_str());
        http->addHeader("Accept", "application/octet-stream");
    });

    net::disconnect();

    switch (updateResult) {
    case HTTP_UPDATE_OK:
        result.code = ResultCode::Success;
        result.summary = "Update ready";
        result.detail = result.latestVersion;
        result.rebootRequired = true;
        return result;
    case HTTP_UPDATE_NO_UPDATES:
        result.code = ResultCode::NoUpdate;
        result.summary = "Already current";
        result.detail = result.latestVersion;
        return result;
    case HTTP_UPDATE_FAILED:
    default:
        result.code = ResultCode::InstallFailed;
        result.summary = "Update failed";
        const String error = updater.getLastErrorString();
        result.detail.assign(error.c_str(), error.length());
        return result;
    }
}
