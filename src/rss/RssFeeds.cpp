#include "rss/RssFeeds.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <algorithm>
#include <vector>
#include "board/BoardStorage.h"

#include "net/WifiConnection.h"
#include "rss/FeedParser.h"
#include "rss/RssConfig.h"
#include "rss/RssConfigStorage.h"
#include "storage/fs/StorageFiles.h"
#include "storage/fs/StoragePaths.h"
#include "text/AsciiText.h"

namespace {

    constexpr const char* kStatusTitle = "RSS";
    constexpr uint32_t kFeedRequestTimeoutMs = 30000;
    constexpr uint32_t kFeedTotalTimeoutMs = 90000;
    constexpr uint32_t kFeedIdleTimeoutMs = 15000;
    constexpr uint32_t kFeedProgressIntervalMs = 1000;
    constexpr size_t kMaxFeedBytes = 4UL * 1024UL * 1024UL;
    constexpr uint8_t kMaxFeedsPerCheck = 8;
    constexpr uint8_t kMaxItemsPerFeed = 5;
    constexpr uint8_t kMaxArticlesPerCheck = 12;
    constexpr uint8_t kMaxFeedRedirects = 3;

    bool ensureArticleDirectory() {
        return StorageFiles::ensureDirectory(StoragePaths::kBooksPath, "rss")
            && StorageFiles::ensureDirectory(StoragePaths::kArticleFilesPath, "rss");
    }

    String trimCopy(String value) {
        value.trim();
        return value;
    }

    bool startsWithHttp(const String& url) {
        String lowered = trimCopy(url);
        lowered.toLowerCase();
        return lowered.startsWith("http://") || lowered.startsWith("https://");
    }

    bool isSafeFilenameChar(char c) {
        return AsciiText::isAlphaNumeric(c) || c == '-' || c == '_' || c == ' ' || c == '.';
    }

    String userAgent() {
        return String("RSVP-Nano-RSS/1.0");
    }

    String feedProgressLabel(uint8_t feedIndex, uint8_t feedCount) {
        return "Feed " + String(feedIndex) + "/" + String(feedCount);
    }

    bool isRedirectStatus(int statusCode) {
        return statusCode == HTTP_CODE_MOVED_PERMANENTLY || statusCode == HTTP_CODE_FOUND
            || statusCode == HTTP_CODE_SEE_OTHER || statusCode == HTTP_CODE_TEMPORARY_REDIRECT
            || statusCode == HTTP_CODE_PERMANENT_REDIRECT;
    }

    String friendlyHttpError(int statusCode) {
        switch (statusCode) {
        case HTTPC_ERROR_CONNECTION_REFUSED:
            return "Could not reach feed";
        case HTTPC_ERROR_SEND_HEADER_FAILED:
        case HTTPC_ERROR_SEND_PAYLOAD_FAILED:
            return "Connection failed";
        case HTTPC_ERROR_NOT_CONNECTED:
            return "Wi-Fi dropped out";
        case HTTPC_ERROR_CONNECTION_LOST:
            return "Connection was lost";
        case HTTPC_ERROR_NO_STREAM:
            return "No data from site";
        case HTTPC_ERROR_NO_HTTP_SERVER:
            return "Not a web feed";
        case HTTPC_ERROR_TOO_LESS_RAM:
            return "Feed is too large";
        case HTTPC_ERROR_ENCODING:
            return "Feed format not supported";
        case HTTPC_ERROR_STREAM_WRITE:
            return "Could not read feed";
        case HTTPC_ERROR_READ_TIMEOUT:
            return "Site took too long";
        }

        switch (statusCode) {
        case HTTP_CODE_BAD_REQUEST:
            return "Feed link looks wrong";
        case HTTP_CODE_UNAUTHORIZED:
            return "Feed needs login";
        case HTTP_CODE_FORBIDDEN:
            return "Site blocked reader";
        case HTTP_CODE_NOT_FOUND:
            return "Feed not found";
        case HTTP_CODE_REQUEST_TIMEOUT:
            return "Site took too long";
        case HTTP_CODE_TOO_MANY_REQUESTS:
            return "Site says try later";
        }

        if (statusCode >= 500 && statusCode < 600) {
            return "Site is having trouble";
        }
        if (statusCode >= 300 && statusCode < 400) {
            return "Feed moved unexpectedly";
        }
        return "Could not download feed";
    }

    String urlScheme(const String& url) {
        const int marker = url.indexOf("://");
        if (marker < 0) {
            return "http";
        }
        return url.substring(0, marker);
    }

    String urlOrigin(const String& url) {
        const int marker = url.indexOf("://");
        const int hostStart = marker < 0 ? 0 : marker + 3;
        int hostEnd = url.indexOf('/', hostStart);
        if (hostEnd < 0) {
            hostEnd = url.length();
        }
        return url.substring(0, hostEnd);
    }

    String resolveRedirectUrl(const String& baseUrl, String location) {
        location.trim();
        if (location.startsWith("http://") || location.startsWith("https://")) {
            return location;
        }
        if (location.startsWith("//")) {
            return urlScheme(baseUrl) + ":" + location;
        }
        if (location.startsWith("/")) {
            return urlOrigin(baseUrl) + location;
        }

        int slash = baseUrl.lastIndexOf('/');
        const int marker = baseUrl.indexOf("://");
        if (slash <= marker + 2) {
            return urlOrigin(baseUrl) + "/" + location;
        }
        return baseUrl.substring(0, slash + 1) + location;
    }

    uint32_t fnv1a(const String& value) {
        uint32_t hash = 2166136261UL;
        for (size_t i = 0; i < value.length(); ++i) {
            hash ^= static_cast<uint8_t>(value[i]);
            hash *= 16777619UL;
        }
        return hash;
    }

    String itemIdentity(const feedparser::FeedItem& item) {
        return item.link.isEmpty() ? item.title : item.link;
    }

    String seenKeyForItem(const feedparser::FeedItem& item) {
        char key[16];
        std::snprintf(key, sizeof(key), "rss%08lx", static_cast<unsigned long>(fnv1a(itemIdentity(item))));
        return String(key);
    }

    bool itemAlreadySeen(const feedparser::FeedItem& item, Preferences& preferences) {
        return preferences.getBool(seenKeyForItem(item).c_str(), false);
    }

    void markItemSeen(const feedparser::FeedItem& item, Preferences& preferences) {
        preferences.putBool(seenKeyForItem(item).c_str(), true);
    }

    String filenameForItem(const feedparser::FeedItem& item) {
        String name = item.title; // already HTML-stripped and entity-decoded by FeedParser
        String cleaned;
        cleaned.reserve(80);
        for (size_t i = 0; i < name.length() && cleaned.length() < 72; ++i) {
            const char c = name[i];
            cleaned += isSafeFilenameChar(c) ? c : '-';
        }
        cleaned.trim();
        while (cleaned.indexOf("--") >= 0) {
            cleaned.replace("--", "-");
        }
        if (cleaned.isEmpty()) {
            cleaned = "rss-article";
        }
        char suffix[16];
        std::snprintf(suffix, sizeof(suffix), "-%08lx", static_cast<unsigned long>(fnv1a(itemIdentity(item))));
        return cleaned + suffix + ".rsvp";
    }

    String metadataSafe(String value) {
        value.replace("\r", " ");
        value.replace("\n", " ");
        value.trim();
        return value;
    }

    void report(RssFeeds::StatusCallback callback, void* context, const String& line1, const String& line2,
                                int progressPercent) {
        if (callback == nullptr) {
            return;
        }
        callback(context, kStatusTitle, line1.c_str(), line2.c_str(), progressPercent);
    }

    bool connectWiFi(const String& wifiSsid, const String& wifiPassword, RssFeeds::StatusCallback callback,
                     void* context) {
        return net::connectStation(wifiSsid.c_str(), wifiPassword.c_str(), [&](int percent) {
            report(callback, context, "Connecting Wi-Fi", wifiSsid, percent);
        });
    }

    void disconnectWiFi() {
        net::disconnect();
    }

    bool fetchUrl(const String& url, String& body, String& error, uint8_t feedIndex, uint8_t feedCount,
                                  RssFeeds::StatusCallback callback, void* context) {
        String currentUrl = url;
        for (uint8_t redirectCount = 0; redirectCount <= kMaxFeedRedirects; ++redirectCount) {
            WiFiClientSecure secureClient;
            WiFiClient plainClient;
            secureClient.setInsecure();
            secureClient.setHandshakeTimeout(15);

            HTTPClient http;
            http.setUserAgent(userAgent());
            http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
            http.setTimeout(kFeedRequestTimeoutMs);
            const char* headers[] = {"Location"};
            http.collectHeaders(headers, 1);

            const bool ok = currentUrl.startsWith("https://") ? http.begin(secureClient, currentUrl)
                                                              : http.begin(plainClient, currentUrl);
            if (!ok) {
                error = "Feed link did not open";
                Serial.printf("[rss] begin failed url=%s\n", currentUrl.c_str());
                return false;
            }

            report(callback, context, feedProgressLabel(feedIndex, feedCount),
                   "Requesting " + feedparser::hostLabelForUrl(currentUrl), 18 + feedIndex * 7);
            const int statusCode = http.GET();
            if (isRedirectStatus(statusCode)) {
                String location = http.header("Location");
                http.end();
                if (location.isEmpty()) {
                    error = "Feed moved but gave no link";
                    Serial.printf("[rss] redirect missing location status=%d url=%s\n", statusCode, currentUrl.c_str());
                    return false;
                }
                currentUrl = resolveRedirectUrl(currentUrl, location);
                Serial.printf("[rss] redirect %u url=%s\n", static_cast<unsigned int>(statusCode), currentUrl.c_str());
                report(callback, context, feedProgressLabel(feedIndex, feedCount),
                       "Redirecting to " + feedparser::hostLabelForUrl(currentUrl), 18 + feedIndex * 7);
                delay(250);
                continue;
            }
            if (statusCode != HTTP_CODE_OK) {
                error = friendlyHttpError(statusCode);
                Serial.printf("[rss] http failed status=%d message=%s url=%s\n", statusCode, error.c_str(),
                              currentUrl.c_str());
                http.end();
                return false;
            }

            WiFiClient* stream = http.getStreamPtr();
            if (stream == nullptr) {
                error = "No data from site";
                Serial.printf("[rss] no stream url=%s\n", currentUrl.c_str());
                http.end();
                return false;
            }

            uint8_t buffer[512];
            size_t totalRead = 0;
            size_t completeItemSearchStart = 0;
            uint8_t completeItemsRead = 0;
            bool stoppedAfterItems = false;
            bool acceptedPartialFeed = false;
            const int reportedSize = http.getSize();
            const size_t reserveBytes =
                reportedSize > 0 ? std::min(static_cast<size_t>(reportedSize), kMaxFeedBytes) : 8192;
            body = "";
            body.reserve(reserveBytes);
            const uint32_t startedMs = millis();
            uint32_t lastByteMs = startedMs;
            uint32_t lastReportMs = 0;
            while (http.connected() || stream->available()) {
                const uint32_t nowMs = millis();
                if (nowMs - startedMs > kFeedTotalTimeoutMs) {
                    if (completeItemsRead > 0) {
                        acceptedPartialFeed = true;
                        Serial.printf("[rss] total timeout after usable items url=%s "
                                      "bytes=%u items=%u\n",
                                      currentUrl.c_str(), static_cast<unsigned int>(totalRead),
                                      static_cast<unsigned int>(completeItemsRead));
                        break;
                    }
                    error = "Site took too long";
                    Serial.printf("[rss] total timeout url=%s bytes=%u\n", currentUrl.c_str(),
                                  static_cast<unsigned int>(totalRead));
                    http.end();
                    return false;
                }
                if (nowMs - lastByteMs > kFeedIdleTimeoutMs) {
                    if (completeItemsRead > 0) {
                        acceptedPartialFeed = true;
                        Serial.printf("[rss] idle after usable items url=%s bytes=%u items=%u\n", currentUrl.c_str(),
                                      static_cast<unsigned int>(totalRead), static_cast<unsigned int>(completeItemsRead));
                        break;
                    }
                    if (totalRead > 0 && feedparser::hasCompleteFeed(body)) {
                        Serial.printf("[rss] idle after complete feed url=%s bytes=%u\n", currentUrl.c_str(),
                                      static_cast<unsigned int>(totalRead));
                        break;
                    }
                    error = "Site stopped sending data";
                    Serial.printf("[rss] idle timeout url=%s bytes=%u\n", currentUrl.c_str(),
                                  static_cast<unsigned int>(totalRead));
                    http.end();
                    return false;
                }
                if (nowMs - lastReportMs >= kFeedProgressIntervalMs) {
                    lastReportMs = nowMs;
                    report(callback, context, feedProgressLabel(feedIndex, feedCount),
                           "Downloaded " + String(static_cast<unsigned int>(totalRead / 1024)) + " KB", 20 + feedIndex * 7);
                }
                if (reportedSize > 0 && totalRead >= static_cast<size_t>(reportedSize)) {
                    break;
                }
                const int available = stream->available();
                if (available <= 0) {
                    delay(1);
                    continue;
                }
                const size_t remaining = kMaxFeedBytes - totalRead;
                if (remaining == 0) {
                    break;
                }
                const size_t chunkSize = std::min(remaining, std::min(sizeof(buffer), static_cast<size_t>(available)));
                const int bytesRead = stream->readBytes(buffer, chunkSize);
                if (bytesRead <= 0) {
                    break;
                }
                lastByteMs = millis();
                const size_t previousRead = totalRead;
                totalRead += static_cast<size_t>(bytesRead);
                for (int i = 0; i < bytesRead; ++i) {
                    body += static_cast<char>(buffer[i]);
                }
                while (completeItemsRead < kMaxItemsPerFeed && feedparser::advancePastItem(body, completeItemSearchStart)) {
                    ++completeItemsRead;
                }
                if (completeItemsRead >= kMaxItemsPerFeed) {
                    stoppedAfterItems = true;
                    Serial.printf("[rss] downloaded item limit url=%s bytes=%u items=%u\n", currentUrl.c_str(),
                                  static_cast<unsigned int>(totalRead), static_cast<unsigned int>(completeItemsRead));
                    break;
                }
                const size_t closeSearchStart = previousRead > 16 ? previousRead - 16 : 0;
                if (feedparser::hasCompleteFeed(body, closeSearchStart)) {
                    break;
                }
            }
            http.end();

            if (body.isEmpty()) {
                error = "Feed was empty";
                Serial.printf("[rss] empty response url=%s\n", currentUrl.c_str());
                return false;
            }
            if (totalRead >= kMaxFeedBytes) {
                Serial.printf("[rss] feed capped url=%s bytes=%u\n", currentUrl.c_str(),
                              static_cast<unsigned int>(totalRead));
                report(callback, context, feedProgressLabel(feedIndex, feedCount),
                       "Reached " + String(static_cast<unsigned int>(kMaxFeedBytes / 1024)) + " KB cap",
                       20 + feedIndex * 7);
                delay(500);
            } else if (stoppedAfterItems) {
                report(callback, context, feedProgressLabel(feedIndex, feedCount),
                       "Downloaded " + String(static_cast<unsigned int>(completeItemsRead)) + " items", 20 + feedIndex * 7);
            } else if (acceptedPartialFeed) {
                report(callback, context, feedProgressLabel(feedIndex, feedCount), "Downloaded partial feed",
                       20 + feedIndex * 7);
            } else {
                report(callback, context, feedProgressLabel(feedIndex, feedCount),
                       "Downloaded " + String(static_cast<unsigned int>(totalRead / 1024)) + " KB", 20 + feedIndex * 7);
            }
            return true;
        }

        error = "Feed redirected too often";
        Serial.printf("[rss] too many redirects url=%s\n", url.c_str());
        return false;
    }

    bool saveItem(const feedparser::FeedItem& item, Preferences& preferences, RssFeeds::Result& result) {
        if (!ensureArticleDirectory()) {
            Serial.println("[rss] article folder unavailable");
            return false;
        }
        const String finalPath = String(StoragePaths::kArticleFilesPath) + "/" + filenameForItem(item);
        const String tmpPath = finalPath + ".tmp";
        Board::Storage::filesystem().remove(tmpPath);

        File file = Board::Storage::filesystem().open(tmpPath, FILE_WRITE);
        if (!file) {
            Serial.printf("[rss] could not create %s\n", tmpPath.c_str());
            return false;
        }

        file.println("@rsvp 1");
        file.print("@title ");
        file.println(metadataSafe(item.title));
        file.print("@author ");
        file.println(metadataSafe(item.author.isEmpty() ? feedparser::sourceLabelForItem(item) : item.author));
        if (!item.link.isEmpty()) {
            file.print("@source ");
            file.println(metadataSafe(item.link));
        }
        file.println();

        String body = item.body;
        if (body.length() > feedparser::kMaxArticleChars) {
            body = body.substring(0, feedparser::kMaxArticleChars);
            body += "\n\n[Article truncated on device.]";
        }
        file.println(body);
        file.close();

        Board::Storage::filesystem().remove(finalPath);
        if (!Board::Storage::filesystem().rename(tmpPath, finalPath)) {
            Board::Storage::filesystem().remove(tmpPath);
            Serial.printf("[rss] rename failed %s\n", finalPath.c_str());
            return false;
        }

        markItemSeen(item, preferences);
        ++result.articlesSaved;
        Serial.printf("[rss] saved %s\n", finalPath.c_str());
        return true;
    }

    bool processFeed(const String& feedUrl, const String& feedBody, Preferences& preferences,
                                     RssFeeds::Result& result, uint8_t feedIndex, uint8_t feedCount, RssFeeds::StatusCallback callback,
                                     void* context) {
        size_t searchStart = 0;
        uint8_t itemCount = 0;
        uint8_t savedBefore = result.articlesSaved;
        uint8_t skippedBefore = result.articlesSkipped;
        report(callback, context, feedProgressLabel(feedIndex, feedCount), "Parsing items", 24 + feedIndex * 7);
        while (itemCount < kMaxItemsPerFeed && result.articlesSaved < kMaxArticlesPerCheck) {
            feedparser::FeedItem item;
            if (!feedparser::parseNextItem(feedBody, searchStart, item)) {
                break;
            }
            ++itemCount;
            if (itemAlreadySeen(item, preferences)) {
                ++result.articlesSkipped;
                report(callback, context, feedProgressLabel(feedIndex, feedCount),
                       "Already synced " + String(itemCount) + "/" + String(kMaxItemsPerFeed), 24 + feedIndex * 7);
                continue;
            }
            report(callback, context, "Saving article " + String(itemCount), item.title, 24 + feedIndex * 7);
            saveItem(item, preferences, result);
        }
        const uint8_t savedHere = result.articlesSaved - savedBefore;
        const uint8_t skippedHere = result.articlesSkipped - skippedBefore;
        if (itemCount == 0) {
            report(callback, context, feedProgressLabel(feedIndex, feedCount), "No usable items", 24 + feedIndex * 7);
        } else {
            report(callback, context, feedProgressLabel(feedIndex, feedCount),
                   String(savedHere) + " saved, " + String(skippedHere) + " skipped", 24 + feedIndex * 7);
        }
        Serial.printf("[rss] feed url=%s items=%u saved=%u skipped=%u\n", feedUrl.c_str(),
                      static_cast<unsigned int>(itemCount), static_cast<unsigned int>(savedHere),
                      static_cast<unsigned int>(skippedHere));
        delay(600);
        return itemCount > 0;
    }
} // namespace

RssFeeds::Result RssFeeds::check(Preferences& preferences, const settings::DeviceSettings& settings,
                                const settings::DeviceSecrets& secrets, StatusCallback callback, void* context) {
    const String wifiSsid = settings.network.wifiSsid.c_str();
    const String wifiPassword = secrets.wifiPassword.c_str();

    Result result;
    if (trimCopy(wifiSsid).isEmpty()) {
        result.summary = "Wi-Fi not set";
        result.detail = "Settings -> Wi-Fi";
        return result;
    }

    if (!connectWiFi(wifiSsid, wifiPassword, callback, context)) {
        disconnectWiFi();
        result.summary = "Wi-Fi failed";
        result.detail = "Check credentials";
        return result;
    }

    auto config = rss::load(Board::Storage::filesystem());
    if (!config) {
        disconnectWiFi();
        result.summary = config.error() == std::errc::no_such_file_or_directory ? "No feeds" : "Invalid feeds";
        result.detail = StoragePaths::kRssConfigPath;
        return result;
    }

    std::vector<String> feeds;
    const size_t feedCount = std::min(config->feeds.size(), static_cast<size_t>(kMaxFeedsPerCheck));
    feeds.reserve(feedCount);
    for (size_t index = 0; index < feedCount; ++index)
        feeds.emplace_back(config->feeds[index].c_str());

    if (feeds.empty()) {
        disconnectWiFi();
        result.summary = "No feed URLs";
        result.detail = StoragePaths::kRssConfigPath;
        return result;
    }

    uint8_t feedFailures = 0;
    String firstFeedError;
    bool mixedFeedErrors = false;

    for (uint8_t feedIndex = 0; feedIndex < feeds.size() && result.articlesSaved < kMaxArticlesPerCheck; ++feedIndex) {
        const String& line = feeds[feedIndex];
        const uint8_t displayIndex = feedIndex + 1;
        const uint8_t feedCount = feeds.size();
        report(callback, context, feedProgressLabel(displayIndex, feedCount),
               "Downloading " + feedparser::hostLabelForUrl(line), 15 + displayIndex * 8);

        String feedBody;
        String error;
        if (!fetchUrl(line, feedBody, error, displayIndex, feedCount, callback, context)) {
            Serial.printf("[rss] feed failed url=%s error=%s\n", line.c_str(), error.c_str());
            ++feedFailures;
            if (firstFeedError.isEmpty()) {
                firstFeedError = error;
            } else if (error != firstFeedError) {
                mixedFeedErrors = true;
            }
            report(callback, context, feedProgressLabel(displayIndex, feedCount), "Skipped: " + error,
                   15 + displayIndex * 8);
            delay(600);
            continue;
        }

        ++result.feedsChecked;
        processFeed(line, feedBody, preferences, result, displayIndex, feedCount, callback, context);
    }

    disconnectWiFi();

    if (result.feedsChecked == 0) {
        result.summary = "Feeds unavailable";
        result.detail = mixedFeedErrors || firstFeedError.isEmpty() ? "Check feed URLs" : firstFeedError;
    } else if (result.articlesSaved == 0) {
        result.summary = "No new articles";
        result.detail = String(result.feedsChecked) + " checked";
        if (feedFailures > 0) {
            result.detail += ", " + String(feedFailures) + " failed";
        }
    } else {
        result.summary = String(result.articlesSaved) + " article" + (result.articlesSaved == 1 ? "" : "s") + " saved";
        result.detail = String(result.feedsChecked) + " checked";
        if (feedFailures > 0) {
            result.detail += ", " + String(feedFailures) + " failed";
        }
    }
    return result;
}
