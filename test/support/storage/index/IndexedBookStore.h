#pragma once

#include <cstddef>
#include <string>

class IndexedBookStore {
public:
    size_t wordCount() const {
        return 0;
    }

    std::string wordAt(size_t) const {
        return {};
    }

    void prefetchAround(size_t) const {}
};
