#include "storage/library/BookLibrary.h"
#include <esp_log.h>

#include <Arduino.h>
#include <algorithm>
#include <vector>
#include "board/BoardStorage.h"

#include "storage/fs/StoragePaths.h"
#include "storage/library/EpubCache.h"
#include "text/AsciiText.h"
#include "text/RsvpDirectives.h"
#include "text/TextNormalizer.h"

namespace BookLibrary {
    namespace {

        using RsvpText::readRsvpDirectiveValues;
        using RsvpText::RsvpDirectiveValues;
        using namespace StoragePaths;

        struct DirectoryEntryInfo {
            std::string path;
            std::string loweredPath;
            size_t bytes = 0;
        };

        struct Counts {
            size_t rsvp = 0;
            size_t text = 0;
            size_t pendingEpub = 0;
        };

        std::vector<DirectoryEntryInfo> scanLibraryDirectories() {
            std::vector<DirectoryEntryInfo> entries;
            auto makeEntryInfo = [](std::string_view directoryPath, std::string_view name, size_t bytes) {
                DirectoryEntryInfo info;
                info.path.reserve(directoryPath.size() + name.size() + 1);
                info.path.append(directoryPath).append("/").append(name);
                info.loweredPath = info.path;
                std::ranges::transform(info.loweredPath, info.loweredPath.begin(), AsciiText::toLower);
                info.bytes = bytes;
                return info;
            };
            auto appendDirectoryEntries = [&](const char* directoryPath) {
                File dir = Board::Storage::filesystem().open(directoryPath);
                if (!dir || !dir.isDirectory()) {
                    if (dir) {
                        dir.close();
                    }
                    return;
                }

                for (File entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
                    if (!entry.isDirectory()) {
                        const std::string name = StoragePaths::displayNameForPath(entry.name());

                        if (!name.empty())
                            entries.push_back(makeEntryInfo(directoryPath, name, static_cast<size_t>(entry.size())));
                    }

                    entry.close();
                }

                dir.close();
            };

            appendDirectoryEntries(StoragePaths::kBooksPath);
            appendDirectoryEntries(StoragePaths::kBookFilesPath);
            appendDirectoryEntries(StoragePaths::kArticleFilesPath);
            return entries;
        }

        bool inventoryHasFileWithBytes(const std::vector<DirectoryEntryInfo>& entries, std::string_view path) {
            std::string loweredPath{path};
            std::ranges::transform(loweredPath, loweredPath.begin(), AsciiText::toLower);

            return std::ranges::any_of(entries, [&](const DirectoryEntryInfo& candidate) {
                return candidate.loweredPath == loweredPath && candidate.bytes > 0;
            });
        }

        std::vector<std::string> collectBookPaths(bool onDeviceEpubConversionEnabled) {
            std::vector<std::string> bookPaths;
            const uint32_t startedMs = millis();
            const std::vector<DirectoryEntryInfo> entries = scanLibraryDirectories();
            size_t cacheProbeCount = 0;

            auto hasStaleGeneratedRsvp = [&](std::string_view path) {
                if (!StoragePaths::hasRsvpExtension(path)
                    || !inventoryHasFileWithBytes(entries, StoragePaths::epubSiblingPathForRsvp(path))) {
                    return false;
                }
                ++cacheProbeCount;
                return !EpubCache::rsvpIsCurrent(path);
            };

            auto isReadableText = [&](std::string_view path) {
                return StoragePaths::hasTextExtension(path)
                    && !inventoryHasFileWithBytes(entries,
                                                  StoragePaths::siblingPathWithExtension(path,
                                                                                         StoragePaths::kRsvpExtension));
            };

            auto isPendingEpub = [&](std::string_view path) {
                if (!onDeviceEpubConversionEnabled || !StoragePaths::hasEpubExtension(path)) {
                    return false;
                }

                const std::string rsvpPath = StoragePaths::rsvpCachePathForEpub(path);
                if (!inventoryHasFileWithBytes(entries, rsvpPath)) {
                    return true;
                }

                ++cacheProbeCount;
                return !EpubCache::hasCurrentCache(path);
            };

            for (const DirectoryEntryInfo& entry: entries) {
                const std::string& path = entry.path;
                if (StoragePaths::isHiddenOrSidecarPath(path)) {
                    continue;
                }

                if ((!hasStaleGeneratedRsvp(path) && StoragePaths::hasRsvpExtension(path)) || isReadableText(path)
                    || isPendingEpub(path)) {
                    bookPaths.push_back(path);
                }
            }

            std::ranges::sort(bookPaths, [](const std::string& left, const std::string& right) {
                std::string leftKey = StoragePaths::displayNameForPath(left);
                std::string rightKey = StoragePaths::displayNameForPath(right);
                std::ranges::transform(leftKey, leftKey.begin(), AsciiText::toLower);
                std::ranges::transform(rightKey, rightKey.begin(), AsciiText::toLower);
                return leftKey < rightKey;
            });

            ESP_LOGD("storage", "Directory inventory: %u files, %u books, %u cache probes in %lu ms",
                     static_cast<unsigned int>(entries.size()), static_cast<unsigned int>(bookPaths.size()),
                     static_cast<unsigned int>(cacheProbeCount), static_cast<unsigned long>(millis() - startedMs));

            return bookPaths;
        }

    } // namespace

    using RsvpText::normalizeDisplayText;
    using namespace StoragePaths;

    void clear(Listing& listing) {
        listing.paths.clear();
        listing.titles.clear();
        listing.authors.clear();
    }

    void refresh(Listing& listing, bool includeMetadata, bool onDeviceEpubConversionEnabled) {
        listing.paths = collectBookPaths(onDeviceEpubConversionEnabled);

        const Counts counts = [&]() {
            Counts counts;
            counts.rsvp = std::ranges::count_if(listing.paths, [](const std::string& path) {
                return hasRsvpExtension(path);
            });
            counts.text = std::ranges::count_if(listing.paths, [](const std::string& path) {
                return hasTextExtension(path);
            });
            counts.pendingEpub = std::ranges::count_if(listing.paths, [](const std::string& path) {
                return hasEpubExtension(path);
            });
            return counts;
        }();

        auto rebuildMetadata = [&]() {
            listing.titles.clear();
            listing.authors.clear();
            listing.titles.reserve(listing.paths.size());
            listing.authors.reserve(listing.paths.size());

            const uint32_t startedMs = millis();
            size_t rsvpMetadataCount = 0;
            for (const std::string& storedPath: listing.paths) {
                std::string title;
                std::string author;

                if (hasRsvpExtension(storedPath)) {
                    const RsvpDirectiveValues values = readRsvpDirectiveValues(storedPath);
                    title = values.title;
                    author = values.author;
                    ++rsvpMetadataCount;
                } else if (hasEpubExtension(storedPath)) {
                    author = EpubCache::libraryLabel(storedPath);
                }

                listing.titles.push_back(std::move(title));
                listing.authors.push_back(std::move(author));
            }

            ESP_LOGD("storage", "Metadata cache: %u entries (%u rsvp) in %lu ms",
                     static_cast<unsigned int>(listing.paths.size()), static_cast<unsigned int>(rsvpMetadataCount),
                     static_cast<unsigned long>(millis() - startedMs));
        };

        // Metadata is optional for fast startup scans, but counts are always logged.
        if (includeMetadata) {
            rebuildMetadata();
        } else {
            listing.titles.clear();
            listing.authors.clear();
            ESP_LOGW("storage", "Metadata cache skipped for %u entries",
                     static_cast<unsigned int>(listing.paths.size()));
        }

        ESP_LOGD("storage", "Library scan: %u books (%u rsvp, %u txt, %u pending epub)",
                 static_cast<unsigned int>(listing.paths.size()), static_cast<unsigned int>(counts.rsvp),
                 static_cast<unsigned int>(counts.text), static_cast<unsigned int>(counts.pendingEpub));
    }

    std::string pathAt(const Listing& listing, size_t index) {
        if (index >= listing.paths.size()) {
            return "";
        }
        return listing.paths[index];
    }

    bool isArticle(const Listing& listing, size_t index) {
        const std::string path = pathAt(listing, index);
        return std::string_view{path}.starts_with(kArticleFilesPrefix);
    }

    std::string displayName(const Listing& listing, size_t index) {
        const std::string path = pathAt(listing, index);
        if (path.empty()) {
            return "";
        }

        if (index < listing.titles.size() && !listing.titles[index].empty()) {
            return listing.titles[index];
        }

        return normalizeDisplayText(displayNameWithoutExtension(path));
    }

    std::string authorName(const Listing& listing, size_t index) {
        const std::string path = pathAt(listing, index);
        if (path.empty()) {
            return "";
        }

        if (index < listing.authors.size()) {
            return listing.authors[index];
        }

        if (hasEpubExtension(path)) {
            return EpubCache::libraryLabel(path);
        }

        const RsvpDirectiveValues values = readRsvpDirectiveValues(path.c_str());
        return values.author;
    }

    int indexOfPath(const Listing& listing, std::string_view target) {
        const auto item = std::ranges::find(listing.paths, target);
        if (item == listing.paths.end()) {
            return -1;
        }
        return static_cast<int>(std::distance(listing.paths.begin(), item));
    }

} // namespace BookLibrary
