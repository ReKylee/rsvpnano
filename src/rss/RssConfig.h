#pragma once

#include <glaze/toml.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace rss {

    constexpr uint32_t kSchemaVersion = 1;
    constexpr size_t kMaxConfigBytes = 4096;
    constexpr size_t kMaxFeeds = 24;

    struct Config {
        uint32_t schemaVersion = kSchemaVersion;
        std::vector<std::string> feeds;
    };

    inline std::expected<void, std::error_code> normalize(Config& config) {
        if (config.schemaVersion != kSchemaVersion)
            return std::unexpected(std::make_error_code(std::errc::not_supported));

        std::erase_if(config.feeds, [](std::string& feed) {
            const size_t first = feed.find_first_not_of(" \t\r\n");
            if (first == std::string::npos)
                return true;
            feed.erase(feed.find_last_not_of(" \t\r\n") + 1);
            feed.erase(0, first);
            return false;
        });
        if (std::ranges::any_of(config.feeds, [](const std::string& feed) {
                return !feed.starts_with("http://") && !feed.starts_with("https://");
            }))
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));

        std::vector<std::string> feeds;
        feeds.reserve(std::min(config.feeds.size(), kMaxFeeds));
        for (std::string& feed: config.feeds)
            if (!std::ranges::contains(feeds, feed))
                feeds.push_back(std::move(feed));
        if (feeds.size() > kMaxFeeds)
            return std::unexpected(std::make_error_code(std::errc::no_buffer_space));
        config.feeds = std::move(feeds);
        return {};
    }

    inline std::expected<Config, std::error_code> decodeToml(std::string_view input) {
        if (input.empty() || input.size() > kMaxConfigBytes)
            return std::unexpected(std::make_error_code(input.empty() ? std::errc::invalid_argument
                                                                      : std::errc::value_too_large));
        Config config;
        if (glz::read_toml(config, input))
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        return normalize(config).transform([&config] {
            return std::move(config);
        });
    }

    inline std::expected<std::string, std::error_code> encodeToml(Config config) {
        return normalize(config).and_then([&config]() -> std::expected<std::string, std::error_code> {
            std::string output;
            if (glz::write_toml(config, output))
                return std::unexpected(std::make_error_code(std::errc::io_error));
            if (output.size() > kMaxConfigBytes)
                return std::unexpected(std::make_error_code(std::errc::value_too_large));
            return output;
        });
    }

} // namespace rss
