#include "update/OtaUpdater.h"

#include <algorithm>
#include <string>

#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "FirmwareVersion.generated.h"
#include "net/WifiConnection.h"
#include "update/ReleaseParser.h"

namespace {

    constexpr size_t kMaxReleaseJsonBytes = 32768;
    constexpr const char* kStatusTitle = "OTA";
    const char* kRedirectHeaderKeys[] = {
        "Location",
    };

    struct ReleaseSource {
        String owner;
        String repo;
        String tag;
    };

    String trimCopy(String value) {
        value.trim();
        return value;
    }

    std::string toStdString(const String& value) {
        return {value.c_str(), value.length()};
    }

    bool isUrlUnreserved(char value) {
        return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9')
            || value == '-' || value == '.' || value == '_' || value == '~';
    }

    String urlEncodePathSegment(const String& value) {
        constexpr char kHex[] = "0123456789ABCDEF";
        String encoded;
        encoded.reserve(value.length());
        for (size_t i = 0; i < value.length(); ++i) {
            const char c = value[i];
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

    bool splitOwnerRepo(const String& value, String& owner, String& repo) {
        const String trimmed = trimCopy(value);
        const int slash = trimmed.indexOf('/');
        if (slash <= 0 || slash >= static_cast<int>(trimmed.length() - 1)) {
            return false;
        }

        owner = trimmed.substring(0, slash);
        repo = trimmed.substring(slash + 1);
        owner.trim();
        repo.trim();
        return !owner.isEmpty() && !repo.isEmpty();
    }

    ReleaseSource releaseSourceForConfig(const OtaUpdater::Config& config) {
        ReleaseSource source{trimCopy(config.githubOwner), trimCopy(config.githubRepo), trimCopy(config.githubTag)};

        splitOwnerRepo(source.owner, source.owner, source.repo);
        splitOwnerRepo(source.repo, source.owner, source.repo);

        const int at = source.tag.indexOf('@');
        if (at > 0 && at < static_cast<int>(source.tag.length() - 1)) {
            String repoPart = source.tag.substring(0, at);
            source.tag = source.tag.substring(at + 1);
            source.tag.trim();
            repoPart.trim();
            if (!splitOwnerRepo(repoPart, source.owner, source.repo) && !repoPart.isEmpty()) {
                source.repo = repoPart;
            }
        }

        return source;
    }

    String httpClientErrorDetail(const String& prefix, int statusCode) {
        if (statusCode >= 0) {
            return prefix + " HTTP " + String(statusCode);
        }

        return prefix + " " + HTTPClient::errorToString(statusCode);
    }

    String readBodyLimited(HTTPClient& http, size_t maxBytes) {
        WiFiClient* stream = http.getStreamPtr();
        if (stream == nullptr) {
            return "";
        }

        const int reportedSize = http.getSize();
        String body;
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
            for (int i = 0; i < bytesRead; ++i) {
                body += static_cast<char>(buffer[i]);
            }
        }

        return body;
    }

    String userAgentForVersion(const String& version) {
        return String("RSVP-Nano/") + (version.isEmpty() ? "dev" : version);
    }

    String versionDetail(const String& currentVersion, const String& latestVersion) {
        if (latestVersion.isEmpty()) {
            return currentVersion;
        }
        if (currentVersion.isEmpty()) {
            return latestVersion;
        }
        return currentVersion + " -> " + latestVersion;
    }

    std::expected<void, std::string> validateAssetName(const String& assetName) {
        const String trimmed = trimCopy(assetName);
        if (trimmed.isEmpty())
            return std::unexpected(std::string{"Asset name missing"});

        if (trimmed != Board::Config::OTA_ASSET_NAME)
            return std::unexpected(toStdString("Asset does not match " + String(Board::Config::BOARD_LABEL)));

        return {};
    }

} // namespace

OtaUpdater::Config OtaUpdater::config(const settings::DeviceSettings& settings,
                                      const settings::DeviceSecrets& secrets) const {
    Config result;
    if (!settings.network.wifiSsid.empty()) {
        result.wifiSsid = settings.network.wifiSsid.c_str();
        result.wifiPassword = secrets.wifiPassword.c_str();
    }

    if (!settings.updates.repositoryOwner.empty())
        result.githubOwner = settings.updates.repositoryOwner.c_str();
    result.githubTag = settings.updates.releaseTag.c_str();
    return result;
}

bool OtaUpdater::isConfigured(const Config& config) const {
    return !trimCopy(config.wifiSsid).isEmpty();
}

String OtaUpdater::currentVersion() const {
    return kFirmwareVersion;
}

bool OtaUpdater::connectWiFi(const Config& config, StatusCallback callback, void* context) const {
    return net::connectStation(config.wifiSsid.c_str(), config.wifiPassword.c_str(),
                               [&](int percent) {
                                   reportStatus(callback, context, kStatusTitle, "Connecting Wi-Fi", config.wifiSsid,
                                                percent);
                               })
        .has_value();
}

void OtaUpdater::disconnectWiFi() const {
    net::disconnect();
}

std::expected<OtaUpdater::LatestRelease, std::string> OtaUpdater::fetchRelease(const Config& config,
                                                                               StatusCallback callback,
                                                                               void* context) const {
    const String installedVersion = currentVersion();
    const ReleaseSource source = releaseSourceForConfig(config);
    if (source.owner.isEmpty() || source.repo.isEmpty())
        return std::unexpected(std::string{"GitHub source missing"});

    const String releasePath = source.tag.isEmpty() ? "latest" : "tags/" + urlEncodePathSegment(source.tag);
    const String url = "https://api.github.com/repos/" + source.owner + "/" + source.repo + "/releases/" + releasePath;
    const String sourceLabel = source.tag.isEmpty() ? source.repo : source.repo + ":" + source.tag;

    reportStatus(callback, context, kStatusTitle, "Checking GitHub", sourceLabel, 22);

    WiFiClientSecure client;
    // GitHub release metadata and assets can redirect across multiple hosts, so
    // keep the transport flexible for now. A signed manifest is the best
    // follow-up hardening step.
    client.setInsecure();
    client.setHandshakeTimeout(15);

    HTTPClient http;
    http.setUserAgent(userAgentForVersion(installedVersion));
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(15000);
    if (!http.begin(client, url))
        return std::unexpected(std::string{"HTTP begin failed"});

    http.addHeader("Accept", "application/vnd.github+json");
    const int statusCode = http.GET();
    if (statusCode != HTTP_CODE_OK) {
        const String errorDetail = statusCode == HTTP_CODE_NOT_FOUND
                                     ? (source.tag.isEmpty() ? "No published release" : "Release tag not found")
                                     : httpClientErrorDetail("GitHub", statusCode);
        http.end();
        return std::unexpected(toStdString(errorDetail));
    }

    const String body = readBodyLimited(http, kMaxReleaseJsonBytes);
    http.end();

    auto parsed = releaseparser::parse(body, config.assetName);
    if (!parsed)
        return std::unexpected(std::string{"Release tag missing"});
    LatestRelease release;
    release.assetUrl = parsed->assetUrl;

    reportStatus(callback, context, kStatusTitle, "Checking version", parsed->tagName, 25);
    const String commitUrl = "https://api.github.com/repos/" + source.owner + "/" + source.repo + "/commits/"
                           + urlEncodePathSegment(parsed->tagName);
    if (!http.begin(client, commitUrl))
        return std::unexpected(std::string{"Commit lookup failed"});
    http.addHeader("Accept", "application/vnd.github.sha");
    const int commitStatus = http.GET();
    if (commitStatus != HTTP_CODE_OK) {
        const String errorDetail = httpClientErrorDetail("Tag commit", commitStatus);
        http.end();
        return std::unexpected(toStdString(errorDetail));
    }
    String commitSha = readBodyLimited(http, 64);
    http.end();
    auto releaseVersion = releaseparser::versionForCommit(parsed->tagName, commitSha);
    if (!releaseVersion)
        return std::unexpected(std::string{"Tag commit invalid"});
    release.version = std::move(*releaseVersion);

    if (release.assetUrl.isEmpty())
        return std::unexpected(toStdString(config.assetName + " missing"));

    return release;
}

std::expected<String, std::string> OtaUpdater::resolveDownloadUrl(const String& assetUrl, const String& version,
                                                                  StatusCallback callback, void* context) const {
    reportStatus(callback, context, kStatusTitle, "Resolving asset", version, 29);

    WiFiClientSecure client;
    client.setInsecure();
    client.setHandshakeTimeout(15);

    HTTPClient http;
    http.collectHeaders(kRedirectHeaderKeys, 1);
    http.setUserAgent(userAgentForVersion(version));
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.setTimeout(15000);
    if (!http.begin(client, assetUrl))
        return std::unexpected(std::string{"Asset URL failed"});

    http.addHeader("Accept", "application/octet-stream");
    const int statusCode = http.GET();
    if (statusCode == HTTP_CODE_OK) {
        http.end();
        return assetUrl;
    }

    if (statusCode == HTTP_CODE_MOVED_PERMANENTLY || statusCode == HTTP_CODE_FOUND || statusCode == HTTP_CODE_SEE_OTHER
        || statusCode == HTTP_CODE_TEMPORARY_REDIRECT || statusCode == HTTP_CODE_PERMANENT_REDIRECT) {
        String resolvedUrl = http.header("Location");
        http.end();
        if (!resolvedUrl.isEmpty())
            return resolvedUrl;
        return std::unexpected(std::string{"Asset redirect missing"});
    }

    const String errorDetail = httpClientErrorDetail("Asset", statusCode);
    http.end();
    return std::unexpected(toStdString(errorDetail));
}

void OtaUpdater::reportStatus(StatusCallback callback, void* context, const char* title, const String& line1,
                              const String& line2, int progressPercent) const {
    if (callback == nullptr) {
        return;
    }

    callback(context, title, line1.c_str(), line2.c_str(), progressPercent);
}

OtaUpdater::Result OtaUpdater::checkOnly(const Config& config, StatusCallback callback, void* context) const {
    Result result;
    result.currentVersion = currentVersion();

    if (auto compatible = validateAssetName(config.assetName); !compatible) {
        result.code = ResultCode::AssetMismatch;
        result.summary = "Wrong OTA asset";
        result.detail = compatible.error().c_str();
        return result;
    }

    if (!isConfigured(config)) {
        result.code = ResultCode::NotConfigured;
        result.summary = "Wi-Fi not set";
        result.detail = "Settings -> Wi-Fi";
        return result;
    }

    if (!connectWiFi(config, callback, context)) {
        disconnectWiFi();
        result.code = ResultCode::ConnectFailed;
        result.summary = "Wi-Fi failed";
        result.detail = "Check credentials";
        return result;
    }

    auto release = fetchRelease(config, callback, context);
    if (!release) {
        disconnectWiFi();
        result.code = ResultCode::MetadataFailed;
        result.summary = "GitHub failed";
        result.detail = release.error().c_str();
        return result;
    }

    disconnectWiFi();
    result.latestVersion = release->version;
    if (release->version == result.currentVersion) {
        result.code = ResultCode::NoUpdate;
        result.summary = "Already current";
        result.detail = release->version;
        return result;
    }

    if (release->assetUrl.isEmpty()) {
        result.code = ResultCode::AssetMissing;
        result.summary = "Asset missing";
        result.detail = config.assetName;
        return result;
    }

    result.code = ResultCode::UpdateAvailable;
    result.summary = "Update available";
    result.detail = release->version;
    return result;
}

OtaUpdater::Result OtaUpdater::checkAndInstall(const Config& config, StatusCallback callback, void* context) const {
    Result result;
    result.currentVersion = currentVersion();

    if (auto compatible = validateAssetName(config.assetName); !compatible) {
        result.code = ResultCode::AssetMismatch;
        result.summary = "Wrong OTA asset";
        result.detail = compatible.error().c_str();
        return result;
    }

    if (!isConfigured(config)) {
        result.code = ResultCode::NotConfigured;
        result.summary = "Wi-Fi not set";
        result.detail = "Settings -> Wi-Fi";
        return result;
    }

    if (!connectWiFi(config, callback, context)) {
        disconnectWiFi();
        result.code = ResultCode::ConnectFailed;
        result.summary = "Wi-Fi failed";
        result.detail = "Check credentials";
        return result;
    }

    auto release = fetchRelease(config, callback, context);
    if (!release) {
        disconnectWiFi();
        result.code = ResultCode::MetadataFailed;
        result.summary = "GitHub failed";
        result.detail = release.error().c_str();
        return result;
    }

    result.latestVersion = release->version;
    if (release->version == result.currentVersion) {
        disconnectWiFi();
        result.code = ResultCode::NoUpdate;
        result.summary = "Already current";
        result.detail = release->version;
        return result;
    }

    if (release->assetUrl.isEmpty()) {
        disconnectWiFi();
        result.code = ResultCode::AssetMissing;
        result.summary = "Asset missing";
        result.detail = config.assetName;
        return result;
    }

    reportStatus(callback, context, kStatusTitle, "Preparing update",
                 versionDetail(result.currentVersion, result.latestVersion), 28);

    auto resolvedAssetUrl = resolveDownloadUrl(release->assetUrl, result.latestVersion, callback, context);
    if (!resolvedAssetUrl) {
        disconnectWiFi();
        result.code = ResultCode::InstallFailed;
        result.summary = "Asset failed";
        result.detail = resolvedAssetUrl.error().c_str();
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
    updater.onProgress([this, callback, context, &result, &lastReportedProgress](int current, int total) {
        if (total <= 0) {
            reportStatus(callback, context, kStatusTitle, "Downloading update", result.latestVersion, -1);
            return;
        }

        const int progress = 30 + static_cast<int>((static_cast<int64_t>(current) * 65) / total);
        if (progress == lastReportedProgress) {
            return;
        }

        lastReportedProgress = progress;
        reportStatus(callback, context, kStatusTitle, "Downloading update", result.latestVersion, progress);
    });

    const String version = result.currentVersion;
    const t_httpUpdate_return updateResult =
        updater.update(client, *resolvedAssetUrl, version, [version](HTTPClient* http) {
            http->setUserAgent(userAgentForVersion(version));
            http->addHeader("Accept", "application/octet-stream");
        });

    disconnectWiFi();

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
        result.detail = updater.getLastErrorString();
        return result;
    }
}
