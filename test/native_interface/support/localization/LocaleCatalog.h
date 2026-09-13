#pragma once
#include <string>
#include <string_view>
#include <vector>
namespace locales {
    struct InstalledPack { std::string locale; };
    using Catalog = std::vector<InstalledPack>;
    inline int nameLookups = 0;
    inline std::string_view localeName(const Catalog&, std::string_view locale) {
        ++nameLookups;
        return locale;
    }
}
