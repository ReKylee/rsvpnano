#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include "book/BookMetadata.h"
#include "storage/index/IndexedBook.h"
#include "storage/index/ReadingProgress.h"
#include "storage/library/BookLibrary.h"

class IndexedBookStore;

class StorageManager {
public:
    using StatusCallback = IndexedBook::StatusCallback;
    void setStatusCallback(StatusCallback callback, void* context);
    bool begin();
    void end();
    void refreshBooks(bool includeMetadata = true);
    std::expected<void, std::error_code> installBook(std::string_view stagedPath, std::string_view destinationPath);
    std::expected<void, std::error_code> removeBook(std::string_view path);
    bool mounted() const {
        return mounted_;
    }
    bool loadIndexedBook(size_t index, IndexedBookStore& store, BookMetadata& metadata,
                         IndexedBook::OpenRequest request = {});
    std::span<const BookLibrary::Entry> books() const {
        return library_;
    }
    const BookLibrary::Entry* book(size_t index) const;
    int findBook(std::string_view path) const;
    std::optional<reading::BookIdentity> readBookMetadata(size_t index, BookMetadata& metadata);

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
