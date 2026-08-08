#include "storage/StorageManager.h"
#include <esp_log.h>

#include <Arduino.h>
#include <cstdint>
#include "board/BoardStorage.h"

#include "book/BookMetadata.h"
#include "storage/fs/SdCard.h"
#include "storage/index/IndexedBook.h"
#include "text/TextNormalizer.h"

#ifndef RSVP_ON_DEVICE_EPUB_CONVERSION
#define RSVP_ON_DEVICE_EPUB_CONVERSION 0
#endif

namespace {

    constexpr uint64_t kBytesPerMegabyte = 1024ULL * 1024ULL;

    void prepareUiMetadata(BookMetadata& metadata) {
        metadata.title = RsvpText::uiSafeMetadata(metadata.title);
        metadata.author = RsvpText::uiSafeMetadata(metadata.author);
        for (ChapterMarker& chapter: metadata.chapters)
            chapter.title = RsvpText::uiSafeMetadata(chapter.title);
    }

} // namespace

void StorageManager::ignoreStatus(void* context, const char* title, const char* line1, const char* line2,
                                  int progressPercent) {
    (void) context;
    (void) title;
    (void) line1;
    (void) line2;
    (void) progressPercent;
}

void StorageManager::setStatusCallback(StatusCallback callback, void* context) {
    statusCallback_ = callback == nullptr ? &StorageManager::ignoreStatus : callback;
    statusContext_ = callback == nullptr ? nullptr : context;
}

bool StorageManager::begin() {
    mounted_ = false;
    clearBookCache();

    statusCallback_(statusContext_, "SD", "Mounting card", "", 5);
    int mountedFrequencyKhz = 0;
    if (SdCard::mount(mounted_, &mountedFrequencyKhz)) {
        const uint64_t sizeMb = Board::Storage::cardSize() / kBytesPerMegabyte;
        ESP_LOGI("storage", "SD initialized (%llu MB, %d kHz)", sizeMb, mountedFrequencyKhz);
        if (!SdCard::ensureFolderLayout()) {
            statusCallback_(statusContext_, "SD", "Folder setup failed", "Run storage check", 10);
        }
        statusCallback_(statusContext_, "SD", "Scanning books", "EPUB converts on open", 10);
        refreshBookPaths(false);
        return true;
    }

    ESP_LOGE("storage", "SD init failed after retries");
    return false;
}

void StorageManager::end() {
    if (mounted_) {
        Board::Storage::end();
    }
    mounted_ = false;
    clearBookCache();
}

void StorageManager::refreshBooks(bool includeMetadata) {
    refreshBookPaths(includeMetadata);
}

size_t StorageManager::bookCount() const {
    return library_.paths.size();
}

int StorageManager::bookIndex(std::string_view path) const {
    return BookLibrary::indexOfPath(library_, path);
}

std::string StorageManager::bookPath(size_t index) const {
    return BookLibrary::pathAt(library_, index);
}

bool StorageManager::bookIsArticle(size_t index) const {
    return BookLibrary::isArticle(library_, index);
}

std::string StorageManager::bookDisplayName(size_t index) const {
    return BookLibrary::displayName(library_, index);
}

std::string StorageManager::bookAuthorName(size_t index) const {
    return BookLibrary::authorName(library_, index);
}

bool StorageManager::loadIndexedBook(size_t index, IndexedBookStore& store, BookMetadata& metadata,
                                     const IndexedBookLoadOptions& options) {
    if (!mounted_) {
        ESP_LOGE("storage", "SD not mounted, cannot load indexed book");
        statusCallback_(statusContext_, "Book open failed", "SD not mounted", "Check card", 100);
        return false;
    }

    IndexedBook::OpenRequest request;
    request.loadedPath = options.loadedPath;
    request.loadedIndex = options.loadedIndex;
    request.allowIndexBuild = options.allowIndexBuild;
    request.allowEpubConversion = options.allowEpubConversion;
    request.statusCallback = statusCallback_;
    request.statusContext = statusContext_;
    if (!IndexedBook::load(index, library_, store, metadata, request))
        return false;
    prepareUiMetadata(metadata);
    return true;
}

void StorageManager::refreshBookPaths(bool includeMetadata) {
    if (!mounted_) {
        clearBookCache();
        return;
    }

    statusCallback_(statusContext_, "SD", "Reading library", "", 96);
    BookLibrary::refresh(library_, includeMetadata, RSVP_ON_DEVICE_EPUB_CONVERSION);
}

void StorageManager::clearBookCache() {
    BookLibrary::clear(library_);
}
