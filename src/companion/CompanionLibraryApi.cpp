#include "companion/CompanionApi.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "board/BoardStorage.h"
#include "companion/CompanionUpload.h"
#include "hash/Fnv1a.h"
#include "logging/Logger.h"
#include "reader/ReadingLoop.h"
#include "storage/fs/StorageFiles.h"
#include "storage/fs/StoragePaths.h"
#include "storage/index/IndexedBook.h"
#include "storage/index/ReadingProgress.h"
#include "text/AsciiText.h"
#include "text/RsvpDirectives.h"
#include "text/UnicodeText.h"

namespace {

    namespace api = companion::api;
    constexpr size_t kMaxBookUploadBytes = 256UL * 1024UL * 1024UL;

    [[nodiscard]] std::string relativeLibraryName(std::string_view path) {
        const std::string prefix = std::string{StoragePaths::kBooksPath} + "/";
        if (path.starts_with(prefix))
            return std::string{path.substr(prefix.length())};
        return StoragePaths::displayNameForPath(path);
    }

    [[nodiscard]] api::HttpError bookInstallError(std::error_code error) {
        if (error == std::errc::file_exists) {
            return api::httpError(HTTP_CODE_CONFLICT, "already_exists",
                                  "A library item with that name already exists", "name");
        }
        return api::httpError(HTTP_CODE_INTERNAL_SERVER_ERROR, "storage_error", "Book could not be installed");
    }

} // namespace

std::optional<companion::api::LibraryItem> CompanionApi::readBook(size_t index, uint32_t bytes) {
    const std::string path = storage_.bookPath(index);
    if (!StoragePaths::hasRsvpExtension(path) && !StoragePaths::hasTextExtension(path)
        && !StoragePaths::hasEpubExtension(path)) {
        return std::nullopt;
    }

    companion::api::LibraryItem item;
    item.id = bookIdForPath(path);
    item.name = relativeLibraryName(path);
    item.bytes = bytes;

    BookMetadata storedMetadata;
    const BookMetadata* metadata = nullptr;
    uint32_t wordCount = 0;
    uint32_t wordIndex = 0;
    settings::ReadingOverrides overrides;

    const ReadingSession& session = readerScreen_.session;
    if (session.fromStorage && session.path == path && readerScreen_.store.isOpen()) {
        metadata = &session.metadata;
        wordCount = static_cast<uint32_t>(ReadingLoop::wordCount(session));
        wordIndex = static_cast<uint32_t>(session.currentIndex);
        overrides = session.state.overrides;
    } else {
        IndexedBookStore::Header header;
        if (IndexedBook::readMetadata(path, storedMetadata, &header)) {
            metadata = &storedMetadata;
            wordCount = header.wordCount;
            if (const auto state = ReadingProgress::readBookState(path, {header.sourceSize, header.sourceFingerprint,
                                                                         header.wordCount})) {
                wordIndex = state->wordIndex;
                overrides = state->overrides;
            }
        }
    }

    if (metadata != nullptr) {
        item.metadata.title = metadata->title;
        item.metadata.author = metadata->author;
    } else if (StoragePaths::hasRsvpExtension(path)) {
        auto directives = RsvpText::readRsvpDirectiveValues(path);
        item.metadata.title = std::move(directives.title);
        item.metadata.author = std::move(directives.author);
    }
    if (item.metadata.title.empty())
        item.metadata.title = storage_.bookDisplayName(index);
    item.metadata.wordCount = wordCount;
    if (metadata == nullptr)
        return item;

    item.metadata.locale = metadata->locale;
    item.metadata.scripts = UnicodeText::scriptTags(metadata->scriptMask);
    const auto addLanguage = [&](std::string_view locale, uint32_t scripts) {
        if (locale.empty())
            return;

        auto tags = UnicodeText::scriptTags(scripts);
        const auto existing = std::ranges::find(item.metadata.languages, locale, &companion::api::BookLanguage::locale);
        if (existing == item.metadata.languages.end()) {
            item.metadata.languages.push_back({std::string{locale}, std::move(tags)});
            return;
        }

        for (std::string& tag: tags) {
            if (!std::ranges::contains(existing->scripts, tag))
                existing->scripts.push_back(std::move(tag));
        }
    };

    addLanguage(metadata->locale, metadata->textRuns.empty() ? metadata->scriptMask : 0);
    for (const BookTextRun& run: metadata->textRuns)
        addLanguage(run.locale, run.scriptMask);

    item.metadata.chapters.reserve(metadata->chapters.size());
    std::ranges::transform(metadata->chapters, std::back_inserter(item.metadata.chapters),
                           [](const ChapterMarker& chapter) {
                               return companion::api::Chapter{
                                   chapter.title,
                                   static_cast<uint32_t>(chapter.wordIndex),
                               };
                           });

    item.reading = companion::api::BookReading{
        .wordIndex = wordIndex,
        .languageFonts = std::move(overrides.languageFonts),
    };
    return item;
}

companion::api::Result<std::vector<companion::api::LibraryItem>> CompanionApi::getLibrary(httpd_req_t& request) {
    (void) request;
    std::vector<companion::api::LibraryItem> response;
    response.reserve(storage_.bookCount());

    for (size_t index = 0; index < storage_.bookCount(); ++index) {
        const std::string path = storage_.bookPath(index);
        File file = Board::Storage::filesystem().open(path.c_str(), FILE_READ);
        if (!file || file.isDirectory())
            continue;
        if (file.size() > std::numeric_limits<uint32_t>::max())
            continue;

        const uint32_t bytes = static_cast<uint32_t>(file.size());
        file.close();
        if (auto item = readBook(index, bytes))
            response.push_back(std::move(*item));
    }
    return response;
}

companion::api::Result<companion::api::Located<companion::api::LibraryItem>> CompanionApi::
    postLibraryItem(httpd_req_t& request) {
    auto requestedName = requiredQueryParameter(request, "name", "Book filename is required");
    if (!requestedName)
        return std::unexpected(companion::api::closeConnection(std::move(requestedName.error())));

    std::string filename = StoragePaths::sanitizeFilename(*requestedName);
    if (filename.empty()) {
        return std::unexpected(companion::api::httpError(HTTP_CODE_BAD_REQUEST, "invalid_field",
                                                         "Book filename is invalid", "name",
                                                         companion::api::ConnectionPolicy::Close));
    }
    if (!StoragePaths::hasRsvpExtension(filename) && !StoragePaths::hasTextExtension(filename)
        && !StoragePaths::hasEpubExtension(filename)) {
        filename += ".rsvp";
    }

    std::string category = queryParameter(request, "category").value_or("");
    std::ranges::transform(category, category.begin(), AsciiText::toLower);
    const char* targetDirectory =
        category == "article" ? StoragePaths::kArticleFilesPath : StoragePaths::kBookFilesPath;

    for (const char* directory:
         {StoragePaths::kBooksPath, StoragePaths::kBookFilesPath, StoragePaths::kArticleFilesPath}) {
        if (auto created = StorageFiles::ensureDirectory(directory); !created) {
            return std::unexpected(companion::api::httpError(HTTP_CODE_INTERNAL_SERVER_ERROR,
                                                             "storage_error",
                                                             "Library folder unavailable: " + created.error().message(),
                                                             std::nullopt, companion::api::ConnectionPolicy::Close));
        }
    }

    const std::string finalPath = std::string{targetDirectory} + "/" + filename;
    auto upload = companion::TemporaryUpload::receive(request, Board::Storage::filesystem(), finalPath + ".tmp",
                                                      kMaxBookUploadBytes, "Book");
    if (!upload)
        return std::unexpected(std::move(upload.error()));

    auto installed = storage_.installBook(upload->path(), finalPath);
    if (!installed) {
        Logger::failure("companion", "install book", upload->path().c_str(), finalPath.c_str(), installed.error());
        return std::unexpected(bookInstallError(installed.error()));
    }

    libraryScreen_.invalidate();
    auto response = bookResponse(finalPath);
    if (!response) {
        companion::api::HttpError error = std::move(response.error());
        if (auto rollback = storage_.removeBook(finalPath); !rollback) {
            Logger::failure("companion", "rollback book install", finalPath.c_str(), rollback.error());
        }
        libraryScreen_.invalidate();
        return std::unexpected(std::move(error));
    }

    return companion::api::Located<companion::api::LibraryItem>{
        .location = "/api/v2/library/" + bookIdForPath(finalPath),
        .value = std::move(*response),
    };
}

companion::api::Result<> CompanionApi::deleteLibraryItem(httpd_req_t& request) {
    auto id = routeId(request, "/api/v2/library/");
    if (!id)
        return std::unexpected(std::move(id.error()));

    auto path = findBookPath(*id);
    if (!path) {
        return std::unexpected(companion::api::httpError(HTTP_CODE_NOT_FOUND, "book_not_found",
                                                         "Book not found", "id"));
    }
    if (readerScreen_.session.path == *path) {
        return std::unexpected(companion::api::httpError(HTTP_CODE_CONFLICT, "resource_in_use",
                                                         "Close the active book before removing it", "id"));
    }

    std::string bookPath = std::move(*path);
    return storage_.removeBook(bookPath)
        .transform_error([&bookPath](std::error_code error) {
            Logger::failure("companion", "delete book", bookPath.c_str(), error);
            return companion::api::httpError(HTTP_CODE_INTERNAL_SERVER_ERROR, "storage_error",
                                             "Book could not be deleted");
        })
        .transform([this, bookPath = std::move(bookPath)] {
            libraryScreen_.invalidate();
            ESP_LOGD("companion", "deleted %s", bookPath.c_str());
        });
}

companion::api::Result<companion::api::LibraryItem> CompanionApi::bookResponse(std::string_view path) {
    const std::string ownedPath{path};
    File file = Board::Storage::filesystem().open(ownedPath.c_str(), FILE_READ);
    if (!file || file.isDirectory()) {
        return std::unexpected(companion::api::httpError(HTTP_CODE_INTERNAL_SERVER_ERROR,
                                                         "storage_error", "Book could not be read"));
    }
    if (file.size() > std::numeric_limits<uint32_t>::max()) {
        return std::unexpected(companion::api::httpError(HTTP_CODE_INTERNAL_SERVER_ERROR,
                                                         "storage_error", "Book size is unsupported"));
    }

    const uint32_t bytes = static_cast<uint32_t>(file.size());
    file.close();

    const int index = storage_.bookIndex(path);
    if (index < 0) {
        return std::unexpected(companion::api::httpError(HTTP_CODE_INTERNAL_SERVER_ERROR,
                                                         "storage_error", "Book is missing from the library"));
    }

    auto book = readBook(static_cast<size_t>(index), bytes);
    if (!book) {
        return std::unexpected(companion::api::httpError(HTTP_CODE_INTERNAL_SERVER_ERROR,
                                                         "storage_error", "Book could not be read"));
    }
    return std::move(*book);
}

std::string CompanionApi::bookIdForPath(std::string_view path) const {
    const uint32_t hash = Fnv1a::hash(path);
    std::array<char, 8> digits{};
    const auto [end, error] = std::to_chars(digits.data(), digits.data() + digits.size(), hash, 16);
    if (error != std::errc{})
        return "b00000000";

    std::string id{"b"};
    const size_t digitCount = static_cast<size_t>(end - digits.data());
    id.append(digits.size() - digitCount, '0');
    id.append(digits.data(), digitCount);
    return id;
}

std::optional<std::string> CompanionApi::findBookPath(std::string_view id) const {
    for (size_t index = 0; index < storage_.bookCount(); ++index) {
        std::string candidate = storage_.bookPath(index);
        if (bookIdForPath(candidate) == id)
            return candidate;
    }
    return std::nullopt;
}
