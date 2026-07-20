#include "storage/index/ReadingProgress.h"
#include <esp_log.h>
#include "logging/Logger.h"

#include <FS.h>
#include <Preferences.h>
#include <glaze/toml.hpp>

#include <algorithm>

#include "board/BoardStorage.h"
#include "reader/ReadingLoop.h"
#include "settings/SettingsGlaze.h"
#include "storage/StorageManager.h"
#include "storage/fs/StorageFiles.h"
#include "storage/fs/StoragePaths.h"
#include "storage/index/IndexedBookStore.h"

namespace ReadingProgress {
    namespace {

        constexpr size_t kMaxBookStateBytes = 2048;
        constexpr uint32_t kSaveIntervalMs = 15000;

        bool isValidIdentity(const BookIdentity& identity) {
            return identity.sourceSize > 0 && identity.wordCount > 0;
        }

        bool matches(const BookState& state, const BookIdentity& identity) {
            return state.sourceSize == identity.sourceSize && state.sourceFingerprint == identity.sourceFingerprint
                && state.wordCount == identity.wordCount;
        }

        std::expected<BookState, std::error_code> readBookState(const String& bookPath, const BookIdentity& identity) {
            if (bookPath.isEmpty() || !isValidIdentity(identity))
                return std::unexpected(std::make_error_code(std::errc::invalid_argument));

            File file = Board::Storage::filesystem().open(StoragePaths::bookStatePathFor(bookPath), FILE_READ);
            if (!file || file.isDirectory()) {
                if (file)
                    file.close();
                return std::unexpected(std::make_error_code(file ? std::errc::invalid_argument
                                                                 : std::errc::no_such_file_or_directory));
            }
            const size_t size = file.size();
            if (size == 0 || size > kMaxBookStateBytes) {
                file.close();
                return std::unexpected(std::make_error_code(size > kMaxBookStateBytes ? std::errc::value_too_large
                                                                                      : std::errc::invalid_argument));
            }
            std::string input(size, '\0');
            const size_t read = file.read(reinterpret_cast<uint8_t*>(input.data()), input.size());
            file.close();
            if (read != input.size())
                return std::unexpected(std::make_error_code(std::errc::io_error));

            BookState candidate;
            if (const glz::error_ctx error =
                    glz::read<glz::opts{.format = glz::TOML, .error_on_unknown_keys = false}>(candidate, input)) {
                return std::unexpected(std::make_error_code(std::errc::invalid_argument));
            }
            if (!matches(candidate, identity))
                return std::unexpected(std::make_error_code(std::errc::state_not_recoverable));
            candidate.wordIndex = std::min(candidate.wordIndex, identity.wordCount - 1);
            return candidate;
        }

        std::expected<void, std::error_code> writeBookState(const String& bookPath, BookState state) {
            std::string output;
            if (glz::write_toml(state, output))
                return std::unexpected(std::make_error_code(std::errc::io_error));

            const String sidecarPath = StoragePaths::bookStatePathFor(bookPath);
            File file = Board::Storage::filesystem().open(sidecarPath, FILE_WRITE);
            if (!file || file.isDirectory()) {
                if (file)
                    file.close();
                return std::unexpected(std::make_error_code(std::errc::io_error));
            }
            const size_t written = file.write(reinterpret_cast<const uint8_t*>(output.data()), output.size());
            file.close();
            if (written != output.size()) {
                return std::unexpected(std::make_error_code(std::errc::io_error));
            }
            return {};
        }

        std::expected<void, std::error_code> writeSessionSidecar(const Session& session, const IndexedBookStore& store,
                                                                 uint32_t wordIndex, uint32_t wordCount) {
            if (!session.fromStorage || session.path.empty() || !store.isOpen() || wordCount == 0)
                return std::unexpected(std::make_error_code(std::errc::invalid_argument));
            BookState state = session.state;
            state.sourceSize = store.sourceSize();
            state.sourceFingerprint = store.sourceFingerprint();
            state.wordCount = wordCount;
            state.wordIndex = std::min(wordIndex, wordCount - 1);
            return writeBookState(session.path.c_str(), std::move(state));
        }

        std::expected<uint32_t, std::error_code> readSessionSidecar(Session& session, const IndexedBookStore& store,
                                                                    const ReadingLoop& reader) {
            const BookIdentity identity{store.sourceSize(), store.sourceFingerprint(),
                                        static_cast<uint32_t>(reader.wordCount())};
            if (!session.fromStorage || session.path.empty() || !store.isOpen())
                return std::unexpected(std::make_error_code(std::errc::invalid_argument));
            auto state = readBookState(session.path.c_str(), identity);
            if (!state)
                return std::unexpected(state.error());
            session.state = std::move(*state);
            return session.state.wordIndex;
        }

    } // namespace

    std::expected<uint32_t, std::error_code> readBookStatePosition(const String& bookPath,
                                                                   const BookIdentity& identity) {
        return readBookState(bookPath, identity).transform([](const BookState& state) {
            return state.wordIndex;
        });
    }

    std::expected<void, std::error_code> writeBookStatePosition(const String& bookPath, const BookIdentity& identity,
                                                                uint32_t wordIndex) {
        if (bookPath.isEmpty() || !isValidIdentity(identity))
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));

        BookState state = readBookState(bookPath, identity).value_or(BookState{}); // Preserve per-book preferences.
        state.sourceSize = identity.sourceSize;
        state.sourceFingerprint = identity.sourceFingerprint;
        state.wordCount = identity.wordCount;
        state.wordIndex = std::min(wordIndex, identity.wordCount - 1);
        if (auto written = writeBookState(bookPath, std::move(state)); !written)
            return written;

        ESP_LOGI("storage-progress", "mirrored position word=%u count=%u state=%s",
                     static_cast<unsigned int>(wordIndex), static_cast<unsigned int>(identity.wordCount),
                     StoragePaths::bookStatePathFor(bookPath).c_str());
        return {};
    }

    uint8_t percent(uint32_t wordIndex, uint32_t wordCount) {
        if (wordCount <= 1)
            return 0;
        return static_cast<uint8_t>(std::min<uint32_t>((wordIndex * 100UL) / (wordCount - 1), 100));
    }

    void Session::save(Preferences& preferences, const ReadingLoop& reader, bool force, uint32_t nowMs) {
        if (!fromStorage || path.empty() || (!force && nowMs - lastSaveMs < kSaveIntervalMs))
            return;
        const size_t wordIndex = reader.currentIndex();
        if (!force && wordIndex == lastSavedWordIndex)
            return;
        lastSaveMs = nowMs;
        cache(preferences, reader, static_cast<uint32_t>(wordIndex));
        ESP_LOGI("storage-progress", "saved position word=%u path=%s", static_cast<unsigned int>(wordIndex),
                     path.c_str());
    }

    void Session::cache(Preferences& preferences, const ReadingLoop& reader, uint32_t wordIndex) {
        if (!fromStorage || path.empty())
            return;
        const size_t wordCount = reader.wordCount();
        if (wordCount > 0)
            wordIndex = std::min<uint32_t>(wordIndex, static_cast<uint32_t>(wordCount - 1));
        preferences.putString("book", path.c_str());
        lastSavedWordIndex = wordIndex;
    }

    void Session::mirror(const IndexedBookStore& store, const ReadingLoop& reader) const {
        auto written = writeSessionSidecar(*this, store, static_cast<uint32_t>(reader.currentIndex()),
                                           static_cast<uint32_t>(reader.wordCount()));
        if (!written)
            Logger::failure("storage-progress", "mirror", StoragePaths::bookStatePathFor(path.c_str()).c_str(),
                            written.error());
    }

    uint32_t Session::restore(const IndexedBookStore& store, const ReadingLoop& reader) {
        auto wordIndex = readSessionSidecar(*this, store, reader);
        if (wordIndex)
            return *wordIndex;
        if (wordIndex.error() != std::errc::no_such_file_or_directory
            && wordIndex.error() != std::errc::state_not_recoverable)
            Logger::failure("storage-progress", "restore", StoragePaths::bookStatePathFor(path.c_str()).c_str(),
                            wordIndex.error());
        return kNoSavedWordIndex;
    }

    bool Session::saveChapterTransition(Preferences& preferences, const IndexedBookStore& store, ReadingLoop& reader,
                                        size_t previousWordIndex, size_t currentWordIndex, uint32_t nowMs) {
        if (!fromStorage || path.empty())
            return false;
        for (const ChapterMarker& chapter: metadata.chapters) {
            if (chapter.wordIndex == 0 || chapter.wordIndex <= previousWordIndex
                || chapter.wordIndex > currentWordIndex)
                continue;
            reader.seekTo(chapter.wordIndex);
            save(preferences, reader, true, nowMs);
            mirror(store, reader);
            return true;
        }
        return false;
    }

    std::string Session::title(const StorageManager& storage) const {
        if (!metadata.title.empty())
            return metadata.title;
        if (!fromStorage)
            return "Demo";
        return storage.bookDisplayName(index);
    }

} // namespace ReadingProgress
