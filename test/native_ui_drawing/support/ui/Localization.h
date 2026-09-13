#pragma once
#include <string_view>
enum class UiText { Unknown };
namespace Localization {
    inline constexpr std::string_view kDefaultLocale = "en";
    inline std::string_view text(UiText) { return "unknown"; }
}
