#pragma once

#include <cstddef>
#include <string_view>

class IndexedBookStore {
public:
    bool isOpen() const {
        return false;
    }

    std::string_view sourcePath() const {
        return {};
    }

    size_t wordCount() const {
        return 0;
    }

    std::string_view wordAt(size_t) const {
        return {};
    }

    void prefetchAround(size_t) const {}
};
