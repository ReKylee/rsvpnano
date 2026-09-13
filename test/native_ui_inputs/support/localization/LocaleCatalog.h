#pragma once
#include <vector>
namespace fs { class FS; }
namespace locales {
    struct InstalledPack {};
    struct UiAssets {};
    using Catalog = std::vector<InstalledPack>;
}
