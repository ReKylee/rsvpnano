#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace BookLibrary {

    struct Entry {
        std::string path;
        size_t bytes = 0;
        std::string title;
        std::string author;
        bool metadataLoaded = false;
    };

    using Listing = std::vector<Entry>;

    void refresh(Listing& listing, bool includeMetadata, bool onDeviceEpubConversionEnabled);
    void refreshMetadata(Entry& book);

    const Entry* at(const Listing& listing, size_t index);
    bool isArticle(const Entry& book);
    std::string_view displayName(const Entry& book);
    std::string_view relativeName(const Entry& book);
    std::string id(const Entry& book);
    int indexOfPath(const Listing& listing, std::string_view target);

} // namespace BookLibrary
