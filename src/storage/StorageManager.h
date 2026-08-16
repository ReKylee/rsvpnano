#pragma once

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>

#include "storage/library/BookLibrary.h"

struct BookMetadata;
class IndexedBookStore;

class StorageManager {
public:
    using StatusCallback = void (*)(void* context, const char* title, const char* line1, const char* line2,
                                    int progressPercent);
    struct IndexedBookLoadOptions {
        IndexedBookLoadOptions() :
                loadedPath(nullptr),
                loadedIndex(nullptr),
                allowIndexBuild(true),
                allowEpubConversion(true) {}

        std::string* loadedPath;
        size_t* loadedIndex;
        bool allowIndexBuild;
        bool allowEpubConversion;
    };

    void setStatusCallback(StatusCallback callback, void* context);
    bool begin();
    void end();
    void refreshBooks(bool includeMetadata = true);
    std::expected<void, std::error_code> installBook(std::string_view stagedPath,
                                                      std::string_view destinationPath);
    std::expected<void, std::error_code> removeBook(std::string_view path);
    bool mounted() const {
        return mounted_;
    }
    bool loadIndexedBook(size_t index, IndexedBookStore& store, BookMetadata& metadata,
                         const IndexedBookLoadOptions& options = IndexedBookLoadOptions());
    size_t bookCount() const;
    int bookIndex(std::string_view path) const;
    std::string bookPath(size_t index) const;
    bool bookIsArticle(size_t index) const;
    std::string bookDisplayName(size_t index) const;
    std::string bookAuthorName(size_t index) const;

private:
    static void ignoreStatus(void* context, const char* title, const char* line1, const char* line2,
                             int progressPercent);

    void refreshBookPaths(bool includeMetadata = true);
    void clearBookCache();

    bool mounted_ = false;
    StatusCallback statusCallback_ = &StorageManager::ignoreStatus;
    void* statusContext_ = nullptr;
    BookLibrary::Listing library_;
};
