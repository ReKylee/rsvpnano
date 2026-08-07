#pragma once

#include <expected>
#include <string>
#include <string_view>

namespace LocaleTag {

    std::expected<std::string, std::string> normalize(std::string_view locale);

} // namespace LocaleTag
