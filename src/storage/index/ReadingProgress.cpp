#include "storage/index/ReadingProgress.h"

#include <FS.h>
#include <Preferences.h>
#include <glaze/toml.hpp>

#include <algorithm>

#include "board/BoardStorage.h"
#include "reader/ReadingLoop.h"
#include "settings/SettingsGlaze.h"
#include "storage/StorageManager.h"
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

        bool readBookState(const String& bookPath, const BookIdentity& identity, BookState& state) {
            if (bookPath.isEmpty() || !isValidIdentity(identity))
                return false;

            File file = Board::Storage::filesystem().open(StoragePaths::bookStatePathFor(bookPath), FILE_READ);
            if (!file || file.isDirectory()) {
                if (file)
                    file.close();
                return false;
            }
            const size_t size = file.size();
            if (size == 0 || size > kMaxBookStateBytes) {
                file.close();
                return false;
            }
            std::string input(size, '\0');
            const size_t read = file.read(reinterpret_cast<uint8_t*>(input.data()), input.size());
            file.close();
            if (read != input.size())
                return false;

            BookState candidate;
            if (const glz::error_ctx error = glz::read_toml(candidate, input)) {
                Serial.printf("[storage-progress] invalid book state: %s\n%s\n", bookPath.c_str(),
                              glz::format_error(error, input).c_str());
                return false;
            }
            if (!matches(candidate, identity)) {
                Serial.printf("[storage-progress] ignored stale book state: %s\n", bookPath.c_str());
                return false;
            }
            candidate.wordIndex = std::min(candidate.wordIndex, identity.wordCount - 1);
            state = std::move(candidate);
            return true;
        }

        bool writeBookState(const String& bookPath, BookState state) {
            std::string output;
            if (const glz::error_ctx error = glz::write_toml(state, output)) {
                Serial.printf("[storage-progress] book state serialization failed: %s\n", bookPath.c_str());
                return false;
            }

            const String sidecarPath = StoragePaths::bookStatePathFor(bookPath);
            File file = Board::Storage::filesystem().open(sidecarPath, FILE_WRITE);
            if (!file || file.isDirectory()) {
                if (file)
                    file.close();
                Serial.printf("[storage-progress] book state open failed: %s\n", sidecarPath.c_str());
                return false;
            }
            const size_t written = file.write(reinterpret_cast<const uint8_t*>(output.data()), output.size());
            file.close();
            if (written != output.size()) {
                Serial.printf("[storage-progress] book state write failed: %s\n", sidecarPath.c_str());
                return false;
            }
            return true;
        }

        bool writeSessionSidecar(const Session& session, const IndexedBookStore& store, uint32_t wordIndex,
                                 uint32_t wordCount) {
            if (!session.fromStorage || session.path.empty() || !store.isOpen() || wordCount == 0)
                return false;
            BookState state = session.state;
            state.sourceSize = store.sourceSize();
            state.sourceFingerprint = store.sourceFingerprint();
            state.wordCount = wordCount;
            state.wordIndex = std::min(wordIndex, wordCount - 1);
            return writeBookState(session.path.c_str(), std::move(state));
        }

        bool readSessionSidecar(Session& session, const IndexedBookStore& store, const ReadingLoop& reader,
                                uint32_t& wordIndex) {
            wordIndex = kNoSavedWordIndex;
            const BookIdentity identity{store.sourceSize(), store.sourceFingerprint(),
                                        static_cast<uint32_t>(reader.wordCount())};
            if (!session.fromStorage || session.path.empty() || !store.isOpen()
                || !readBookState(session.path.c_str(), identity, session.state))
                return false;
            wordIndex = session.state.wordIndex;
            return true;
        }

    } // namespace

    bool readBookStatePosition(const String& bookPath, const BookIdentity& identity, uint32_t& wordIndex) {
        wordIndex = 0;
        BookState state;
        if (!readBookState(bookPath, identity, state))
            return false;
        wordIndex = state.wordIndex;
        return true;
    }

    bool writeBookStatePosition(const String& bookPath, const BookIdentity& identity, uint32_t wordIndex) {
        if (bookPath.isEmpty() || !isValidIdentity(identity))
            return false;

        BookState state;
        readBookState(bookPath, identity, state); // Preserve per-book preferences during position-only writes.
        state.sourceSize = identity.sourceSize;
        state.sourceFingerprint = identity.sourceFingerprint;
        state.wordCount = identity.wordCount;
        state.wordIndex = std::min(wordIndex, identity.wordCount - 1);
        if (!writeBookState(bookPath, std::move(state)))
            return false;

        Serial.printf("[storage-progress] mirrored position word=%u count=%u state=%s\n",
                      static_cast<unsigned int>(wordIndex), static_cast<unsigned int>(identity.wordCount),
                      StoragePaths::bookStatePathFor(bookPath).c_str());
        return true;
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
        Serial.printf("[storage-progress] saved position word=%u path=%s\n", static_cast<unsigned int>(wordIndex),
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
        writeSessionSidecar(*this, store, static_cast<uint32_t>(reader.currentIndex()),
                            static_cast<uint32_t>(reader.wordCount()));
    }

    uint32_t Session::restore(const IndexedBookStore& store, const ReadingLoop& reader) {
        uint32_t wordIndex = kNoSavedWordIndex;
        if (readSessionSidecar(*this, store, reader, wordIndex))
            return wordIndex;
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
