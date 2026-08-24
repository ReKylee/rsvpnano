#include "companion/http/CompanionApi.h"

#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "logging/Logger.h"
#include "reader/ReadingLoop.h"
#include "storage/fs/StoragePaths.h"
#include "library/ReadingProgress.h"
#include "text/LocaleTag.h"
#include "text/UnicodeText.h"

namespace api = companion::api;

api::Result<> CompanionApi::putBookPosition(httpd_req_t& request) {
    return readJson<api::BookPositionUpdate>(request, 512, "Position payload exceeds 512 bytes")
        .and_then([this, &request](api::BookPositionUpdate update) -> api::Result<> {
            if (!update.wordIndex) {
                return std::unexpected(api::httpError(HTTP_CODE_BAD_REQUEST, "missing_field", "wordIndex is required",
                                                      "wordIndex"));
            }

            auto id = routeId(request, "/api/v2/library/", "/position");
            if (!id)
                return std::unexpected(std::move(id.error()));

            const auto index = findBookIndex(*id);
            if (!index) {
                return std::unexpected(api::httpError(HTTP_CODE_NOT_FOUND, "book_not_found", "Book not found", "id"));
            }

            const BookLibrary::Entry* book = storage_.book(*index);
            BookMetadata metadata;
            const auto identity = book != nullptr && readerScreen_.session.sourcePath() == book->path
                                    ? std::optional{readerScreen_.store.identity()}
                                    : storage_.readBookMetadata(*index, metadata);
            if (book == nullptr || !identity) {
                return std::unexpected(api::httpError(HTTP_CODE_CONFLICT, "index_unavailable",
                                                      "Book must be indexed on the reader before changing position"));
            }
            if (identity->wordCount == 0) {
                return std::unexpected(api::httpError(HTTP_CODE_CONFLICT, "empty_book", "Book has no readable words"));
            }

            const uint32_t wordIndex = std::min<uint32_t>(*update.wordIndex, identity->wordCount - 1);
            std::string bookPath = book->path;
            return ReadingProgress::writeBookStatePosition(bookPath, *identity, wordIndex)
                .transform_error([&bookPath](std::error_code error) {
                    Logger::failure("companion", "save reading position",
                                    StoragePaths::bookStatePathFor(bookPath).c_str(), error);
                    return api::httpError(HTTP_CODE_INTERNAL_SERVER_ERROR, "storage_error",
                                          "Reading position could not be saved");
                })
                .transform([this, bookPath = std::move(bookPath), wordIndex] {
                    if (readerScreen_.session.sourcePath() != bookPath)
                        return;
                    readerScreen_.session.state.wordIndex = wordIndex;
                    readerScreen_.session.lastSavedWordIndex = wordIndex;
                    ReadingLoop::seekTo(readerScreen_.session, wordIndex);
                });
        });
}

api::Result<> CompanionApi::putBookLanguageFonts(httpd_req_t& request) {
    return readJson<api::BookLanguageFontsUpdate>(request, 2048, "Language font payload exceeds 2 KiB")
        .and_then([this, &request](api::BookLanguageFontsUpdate update) -> api::Result<> {
            auto id = routeId(request, "/api/v2/library/", "/language-fonts");
            if (!id)
                return std::unexpected(std::move(id.error()));

            const auto index = findBookIndex(*id);
            if (!index) {
                return std::unexpected(api::httpError(HTTP_CODE_NOT_FOUND, "book_not_found", "Book not found", "id"));
            }

            const BookLibrary::Entry* book = storage_.book(*index);
            BookMetadata storedMetadata;
            const BookMetadata* metadata = &storedMetadata;
            std::optional<reading::BookIdentity> identity;
            if (book != nullptr && readerScreen_.session.sourcePath() == book->path) {
                metadata = &readerScreen_.session.metadata;
                identity = readerScreen_.store.identity();
            } else {
                identity = storage_.readBookMetadata(*index, storedMetadata);
            }
            if (book == nullptr || !identity || identity->wordCount == 0) {
                return std::unexpected(api::httpError(HTTP_CODE_CONFLICT, "index_unavailable",
                                                      "Book must be indexed before configuring language fonts"));
            }

            for (size_t index = 0; index < update.languageFonts.size(); ++index) {
                auto& selection = update.languageFonts[index];
                const auto preceding = std::span{update.languageFonts}.first(index);

                if (selection.locale == settings::kMathFontTarget) {
                    if ((metadata->scriptMask & UnicodeText::ScriptMath) == 0) {
                        return std::unexpected(api::httpError(HTTP_CODE_UNPROCESSABLE_ENTITY, "invalid_math",
                                                              "Math is not present in this book", "languageFonts"));
                    }
                    if (std::ranges::contains(preceding, settings::kMathFontTarget, &settings::LanguageFont::locale)) {
                        return std::unexpected(api::httpError(HTTP_CODE_UNPROCESSABLE_ENTITY, "duplicate_math",
                                                              "Math can select one font", "languageFonts"));
                    }

                    const auto family = readerScreen_.fonts.find(selection.fontId);
                    if (!family || !family->supports(UnicodeText::ScriptMath)) {
                        return std::unexpected(api::httpError(HTTP_CODE_UNPROCESSABLE_ENTITY, "incompatible_font",
                                                              "Font does not support Math", "languageFonts"));
                    }
                    selection.fontId = family->id;
                    continue;
                }

                auto locale = LocaleTag::normalize(selection.locale);
                if (!locale || !metadata->hasLocale(*locale)) {
                    return std::unexpected(api::httpError(HTTP_CODE_UNPROCESSABLE_ENTITY, "invalid_language",
                                                          "Language is not present in this book", "languageFonts"));
                }
                selection.locale = std::move(*locale);
                if (std::ranges::contains(preceding, selection.locale, &settings::LanguageFont::locale)) {
                    return std::unexpected(api::httpError(HTTP_CODE_UNPROCESSABLE_ENTITY, "duplicate_language",
                                                          "Each language can select one font", "languageFonts"));
                }

                const auto family = readerScreen_.fonts.find(selection.fontId);
                const uint32_t requiredScripts =
                    metadata->scriptsForLocale(selection.locale) & ~UnicodeText::ScriptMath;
                if (requiredScripts == 0 || !family || !family->usableFor(selection.locale, requiredScripts)) {
                    return std::unexpected(api::httpError(HTTP_CODE_UNPROCESSABLE_ENTITY, "incompatible_font",
                                                          "Font does not support this language", "languageFonts"));
                }
                selection.fontId = family->id;
            }

            std::string bookPath = book->path;
            auto languageFonts = std::move(update.languageFonts);
            return ReadingProgress::writeBookLanguageFonts(bookPath, *identity, languageFonts)
                .transform_error([&bookPath](std::error_code error) {
                    Logger::failure("companion", "save language fonts",
                                    StoragePaths::bookStatePathFor(bookPath).c_str(), error);
                    return api::httpError(HTTP_CODE_INTERNAL_SERVER_ERROR, "storage_error",
                                          "Language font choices could not be saved");
                })
                .transform([this, bookPath = std::move(bookPath), languageFonts = std::move(languageFonts)]() mutable {
                    if (readerScreen_.session.sourcePath() != bookPath)
                        return;
                    readerScreen_.session.state.overrides.languageFonts = std::move(languageFonts);
                    readerScreen_.releaseRuntimeCaches();
                });
        });
}
