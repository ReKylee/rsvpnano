#include "storage/library/BookLibrary.h"
#include <esp_log.h>

#include <Arduino.h>
#include <algorithm>
#include <vector>
#include "board/BoardStorage.h"

#include "storage/fs/StoragePaths.h"
#include "storage/library/EpubCache.h"
#include "text/RsvpDirectives.h"
#include "text/TextNormalizer.h"

namespace BookLibrary {
    namespace {

        using RsvpText::readRsvpDirectiveValues;
        using RsvpText::RsvpDirectiveValues;
        using namespace StoragePaths;

        struct DirectoryEntryInfo {
            String path;
            String loweredPath;
            size_t bytes = 0;
        };

        struct Counts {
            size_t rsvp = 0;
            size_t text = 0;
            size_t pendingEpub = 0;
        };

        std::vector<DirectoryEntryInfo> scanLibraryDirectories() {
            std::vector<DirectoryEntryInfo> entries;
            auto makeEntryInfo = [](const char* directoryPath, const String& name, size_t bytes) {
                DirectoryEntryInfo info;
                info.path = String(directoryPath) + "/" + name;
                info.loweredPath = info.path;
                info.loweredPath.toLowerCase();
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
                        const String name = StoragePaths::displayNameForPath(String(entry.name()));

                        if (!name.isEmpty())
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

        bool inventoryHasFileWithBytes(const std::vector<DirectoryEntryInfo>& entries, const String& path) {
            String loweredPath = path;
            loweredPath.toLowerCase();

            return std::any_of(entries.begin(), entries.end(), [&](const DirectoryEntryInfo& candidate) {
                return candidate.loweredPath == loweredPath && candidate.bytes > 0;
            });
        }

        std::vector<std::string> collectBookPaths(bool onDeviceEpubConversionEnabled) {
            std::vector<std::string> bookPaths;
            const uint32_t startedMs = millis();
            const std::vector<DirectoryEntryInfo> entries = scanLibraryDirectories();
            size_t cacheProbeCount = 0;

            auto hasStaleGeneratedRsvp = [&](const String& path) {
                if (!StoragePaths::hasRsvpExtension(path)
                    || !inventoryHasFileWithBytes(entries, StoragePaths::epubSiblingPathForRsvp(path))) {
                    return false;
                }
                ++cacheProbeCount;
                return !EpubCache::rsvpIsCurrent(path);
            };

            auto isReadableText = [&](const String& path) {
                return StoragePaths::hasTextExtension(path)
                    && !inventoryHasFileWithBytes(entries,
                                                  StoragePaths::siblingPathWithExtension(path,
                                                                                         StoragePaths::kRsvpExtension));
            };

            auto isPendingEpub = [&](const String& path) {
                if (!onDeviceEpubConversionEnabled || !StoragePaths::hasEpubExtension(path)) {
                    return false;
                }

                const String rsvpPath = StoragePaths::rsvpCachePathForEpub(path);
                if (!inventoryHasFileWithBytes(entries, rsvpPath)) {
                    return true;
                }

                ++cacheProbeCount;
                return !EpubCache::hasCurrentCache(path);
            };

            for (const DirectoryEntryInfo& entry: entries) {
                const String& path = entry.path;
                if (StoragePaths::isHiddenOrSidecarPath(path)) {
                    continue;
                }

                if ((!hasStaleGeneratedRsvp(path) && StoragePaths::hasRsvpExtension(path)) || isReadableText(path)
                    || isPendingEpub(path)) {
                    bookPaths.emplace_back(path.c_str(), path.length());
                }
            }

            std::sort(bookPaths.begin(), bookPaths.end(), [](const std::string& left, const std::string& right) {
                String leftKey = StoragePaths::displayNameForPath(left.c_str());
                String rightKey = StoragePaths::displayNameForPath(right.c_str());
                leftKey.toLowerCase();
                rightKey.toLowerCase();
                return leftKey < rightKey;
            });

            ESP_LOGD("storage", "Directory inventory: %u files, %u books, %u cache probes in %lu ms",
                          static_cast<unsigned int>(entries.size()), static_cast<unsigned int>(bookPaths.size()),
                          static_cast<unsigned int>(cacheProbeCount), static_cast<unsigned long>(millis() - startedMs));

            return bookPaths;
        }

    } // namespace

    using RsvpText::normalizeDisplayText;
    using RsvpText::readRsvpDirectiveValue;
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
            counts.rsvp = std::count_if(listing.paths.begin(), listing.paths.end(), [](const std::string& path) {
                return hasRsvpExtension(path.c_str());
            });
            counts.text = std::count_if(listing.paths.begin(), listing.paths.end(), [](const std::string& path) {
                return hasTextExtension(path.c_str());
            });
            counts.pendingEpub = std::count_if(listing.paths.begin(), listing.paths.end(), [](const std::string& path) {
                return hasEpubExtension(path.c_str());
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
                const String path{storedPath.c_str()};
                String title;
                String author;

                if (hasRsvpExtension(path)) {
                    const RsvpDirectiveValues values = readRsvpDirectiveValues(path);
                    title = values.title;
                    author = values.author;
                    ++rsvpMetadataCount;
                } else if (hasEpubExtension(path)) {
                    author = EpubCache::libraryLabel(path);
                }

                listing.titles.emplace_back(title.c_str(), title.length());
                listing.authors.emplace_back(author.c_str(), author.length());
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

    void printListing(const Listing& listing) {
        ESP_LOGD("storage",
                      "Listing /books, /books/books, /books/articles (.rsvp/.txt/.epub pending conversion):");
        for (const std::string& path: listing.paths) {
            File entry = Board::Storage::filesystem().open(path.c_str());
            if (!entry || entry.isDirectory()) {
                if (entry) {
                    entry.close();
                }
                continue;
            }

            ESP_LOGD("storage", "  %s (%lu bytes)", path.c_str(), static_cast<unsigned long>(entry.size()));
            entry.close();
        }
    }

    size_t unsupportedFileCount() {
        const std::vector<DirectoryEntryInfo> entries = scanLibraryDirectories();
        return std::count_if(entries.begin(), entries.end(), [](const DirectoryEntryInfo& entry) {
            const String& path = entry.path;
            return !isHiddenOrSidecarPath(path) && !hasRsvpExtension(path) && !hasTextExtension(path)
                && !hasEpubExtension(path);
        });
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

        const String name = normalizeDisplayText(displayNameWithoutExtension(path.c_str()));
        return {name.c_str(), name.length()};
    }

    std::string authorName(const Listing& listing, size_t index) {
        const std::string path = pathAt(listing, index);
        if (path.empty()) {
            return "";
        }

        if (index < listing.authors.size()) {
            return listing.authors[index];
        }

        if (hasEpubExtension(path.c_str())) {
            const String label = EpubCache::libraryLabel(path.c_str());
            return {label.c_str(), label.length()};
        }

        const String author = readRsvpDirectiveValue(path.c_str(), "@author");
        return {author.c_str(), author.length()};
    }

    int indexOfPath(const Listing& listing, std::string_view target) {
        const auto item = std::find_if(listing.paths.begin(), listing.paths.end(), [target](const std::string& path) {
            return path == target;
        });
        if (item == listing.paths.end()) {
            return -1;
        }
        return static_cast<int>(std::distance(listing.paths.begin(), item));
    }

} // namespace BookLibrary
