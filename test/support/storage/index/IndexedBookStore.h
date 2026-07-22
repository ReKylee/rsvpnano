#pragma once

#include <cstddef>
#include <string_view>

class IndexedBookStore {
public:
    size_t wordCount() const {
        return 0;
    }

    std::string_view wordAt(size_t) const {
        return {};
    }

    void prefetchAround(size_t) const {}
};
