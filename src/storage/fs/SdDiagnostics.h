#pragma once

#include <cstddef>
#include <string>

namespace SdDiagnostics {

    struct Inventory {
        size_t libraryItems = 0;
        size_t fonts = 0;
        size_t themes = 0;
    };

    struct Result {
        std::string summary;
        std::string detail;
    };

    Result run(bool mounted, Inventory inventory);

} // namespace SdDiagnostics
