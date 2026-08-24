#pragma once

#include <FS.h>

#include <expected>
#include <system_error>

#include "feeds/RssConfig.h"

namespace rss {

    std::expected<Config, std::error_code> load(fs::FS& filesystem);
    std::expected<void, std::error_code> save(fs::FS& filesystem, Config config);

} // namespace rss
