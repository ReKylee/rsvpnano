#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace BookLibrary {

    struct Listing {
        std::vector<std::string> paths;
        std::vector<std::string> titles;
        std::vector<std::string> authors;
    };

    void clear(Listing& listing);
    void refresh(Listing& listing, bool includeMetadata, bool onDeviceEpubConversionEnabled);
    void printListing(const Listing& listing);
    size_t unsupportedFileCount();

    std::string pathAt(const Listing& listing, size_t index);
    bool isArticle(const Listing& listing, size_t index);
    std::string displayName(const Listing& listing, size_t index);
    std::string authorName(const Listing& listing, size_t index);
    int indexOfPath(const Listing& listing, std::string_view target);

} // namespace BookLibrary
