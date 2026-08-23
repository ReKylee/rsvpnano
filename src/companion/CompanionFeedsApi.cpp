#include "companion/CompanionApi.h"

#include <system_error>
#include <utility>

#include "board/BoardStorage.h"
#include "rss/RssConfig.h"
#include "rss/RssConfigStorage.h"

namespace {

    namespace api = companion::api;

    [[nodiscard]] api::Result<rss::Config> readFeeds() {
        return rss::load(Board::Storage::filesystem())
            .or_else([](std::error_code error) -> std::expected<rss::Config, std::error_code> {
                if (error == std::errc::no_such_file_or_directory)
                    return rss::Config{};
                return std::unexpected(error);
            })
            .transform_error([](std::error_code error) {
                return api::httpError(HTTP_CODE_INTERNAL_SERVER_ERROR, "feed_load_failed", error.message());
            });
    }

    [[nodiscard]] api::HttpError feedSaveError(std::error_code error) {
        if (error == std::errc::invalid_argument) {
            return api::httpError(HTTP_CODE_UNPROCESSABLE_ENTITY, "invalid_feed",
                                  "Feeds must start with http:// or https://", "feeds");
        }
        if (error == std::errc::no_buffer_space) {
            return api::httpError(HTTP_CODE_UNPROCESSABLE_ENTITY, "invalid_feed", "Too many RSS feeds",
                                  "feeds");
        }
        return api::httpError(HTTP_CODE_INTERNAL_SERVER_ERROR, "feed_save_failed", error.message());
    }

} // namespace

auto CompanionApi::getFeeds(httpd_req_t& request) -> companion::api::Result<FeedList> {
    (void) request;
    return readFeeds().transform([](rss::Config config) { return std::move(config.feeds); });
}

companion::api::Result<> CompanionApi::putFeeds(httpd_req_t& request) {
    return readJson<rss::Config>(request, rss::kMaxConfigBytes, "RSS feed payload exceeds 4 KB")
        .and_then([](rss::Config update) -> companion::api::Result<> {
            return rss::save(Board::Storage::filesystem(), std::move(update)).transform_error(feedSaveError);
        });
}
