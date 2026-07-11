#include "storage/index/ReadingProgress.h"

#include <FS.h>
#include <algorithm>
#include <cstdio>

#include "board/BoardStorage.h"
#include "reader/ReadingLoop.h"
#include "settings/PreferenceSpecs.h"
#include "storage/StorageManager.h"
#include "storage/fs/StoragePaths.h"
#include "storage/index/IndexedBookStore.h"

namespace ReadingProgress {
    namespace {

        constexpr const char* kMagic = "rpos";
        constexpr uint32_t kVersion = 1;
        constexpr uint32_t kSaveIntervalMs = 15000;

        bool isValidIdentity(const BookIdentity& identity) {
            return identity.sourceSize > 0 && identity.wordCount > 0;
        }

        uint32_t hashPath(const String& path) {
            uint32_t hash = 2166136261UL;
            for (size_t index = 0; index < path.length(); ++index) {
                hash ^= static_cast<uint8_t>(path[index]);
                hash *= 16777619UL;
            }
            return hash;
        }

        String key(char prefix, const String& path) {
            char value[11];
            std::snprintf(value, sizeof(value), "%c%08lx", prefix, static_cast<unsigned long>(hashPath(path)));
            return String(value);
        }

        bool writeSessionSidecar(const Session& session, const IndexedBookStore& store, uint32_t wordIndex,
                                 uint32_t wordCount) {
            return session.fromStorage && !session.path.isEmpty() && store.isOpen() && wordCount > 0
                && writePositionSidecar(session.path, {store.sourceSize(), store.sourceFingerprint(), wordCount},
                                        wordIndex);
        }

        bool readSessionSidecar(const Session& session, const IndexedBookStore& store, const ReadingLoop& reader,
                                uint32_t& wordIndex) {
            wordIndex = kNoSavedWordIndex;
            const uint32_t wordCount = static_cast<uint32_t>(reader.wordCount());
            return session.fromStorage && !session.path.isEmpty() && store.isOpen() && wordCount > 0
                && readPositionSidecar(session.path, {store.sourceSize(), store.sourceFingerprint(), wordCount},
                                       wordIndex);
        }

    } // namespace

    bool readPositionSidecar(const String& bookPath, const BookIdentity& identity, uint32_t& wordIndex) {
        wordIndex = 0;
        if (bookPath.isEmpty() || !isValidIdentity(identity)) {
            return false;
        }

        File sidecar = Board::Storage::filesystem().open(StoragePaths::progressSidecarPathFor(bookPath), FILE_READ);
        if (!sidecar || sidecar.isDirectory()) {
            if (sidecar) {
                sidecar.close();
            }
            return false;
        }

        const String line = sidecar.readStringUntil('\n');
        sidecar.close();

        char magic[8] = {};
        unsigned long version = 0;
        unsigned long sourceSize = 0;
        unsigned long sourceFingerprint = 0;
        unsigned long wordCount = 0;
        unsigned long savedWordIndex = 0;
        const int parsed = std::sscanf(line.c_str(), "%7s %lu %lu %lu %lu %lu", magic, &version, &sourceSize,
                                       &sourceFingerprint, &wordCount, &savedWordIndex);

        if (parsed != 6 || String(magic) != kMagic || version != kVersion || sourceSize != identity.sourceSize
            || sourceFingerprint != identity.sourceFingerprint || wordCount != identity.wordCount) {
            Serial.printf("[storage-progress] ignored stale progress sidecar: %s\n", bookPath.c_str());
            return false;
        }

        wordIndex = std::min<uint32_t>(static_cast<uint32_t>(savedWordIndex), identity.wordCount - 1);
        return true;
    }

    bool writePositionSidecar(const String& bookPath, const BookIdentity& identity, uint32_t wordIndex) {
        if (bookPath.isEmpty() || !isValidIdentity(identity)) {
            return false;
        }

        const String sidecarPath = StoragePaths::progressSidecarPathFor(bookPath);
        File sidecar = Board::Storage::filesystem().open(sidecarPath, FILE_WRITE);
        if (!sidecar || sidecar.isDirectory()) {
            if (sidecar) {
                sidecar.close();
            }
            Serial.printf("[storage-progress] progress sidecar open failed: %s\n", sidecarPath.c_str());
            return false;
        }

        wordIndex = std::min<uint32_t>(wordIndex, identity.wordCount - 1);
        const size_t written =
            sidecar.printf("%s %lu %lu %lu %lu %lu\n", kMagic, static_cast<unsigned long>(kVersion),
                           static_cast<unsigned long>(identity.sourceSize),
                           static_cast<unsigned long>(identity.sourceFingerprint),
                           static_cast<unsigned long>(identity.wordCount), static_cast<unsigned long>(wordIndex));
        sidecar.close();

        if (written == 0) {
            Serial.printf("[storage-progress] progress sidecar write failed: %s\n", sidecarPath.c_str());
            return false;
        }

        Serial.printf("[storage-progress] mirrored position word=%u count=%u sidecar=%s\n",
                      static_cast<unsigned int>(wordIndex), static_cast<unsigned int>(identity.wordCount),
                      sidecarPath.c_str());
        return true;
    }

    String positionKey(const String& bookPath) {
        return key('p', bookPath);
    }
    String wordCountKey(const String& bookPath) {
        return key('c', bookPath);
    }
    String sourceSizeKey(const String& bookPath) {
        return key('s', bookPath);
    }
    String sourceFingerprintKey(const String& bookPath) {
        return key('f', bookPath);
    }
    String bookId(const String& bookPath) {
        return key('b', bookPath);
    }

    uint8_t percent(uint32_t wordIndex, uint32_t wordCount) {
        if (wordCount <= 1)
            return 0;
        return static_cast<uint8_t>(std::min<uint32_t>((wordIndex * 100UL) / (wordCount - 1), 100));
    }

    void Session::save(Preferences& preferences, const IndexedBookStore& store, const ReadingLoop& reader, bool force,
                       uint32_t nowMs) {
        if (!fromStorage || path.isEmpty() || (!force && nowMs - lastSaveMs < kSaveIntervalMs))
            return;
        const size_t wordIndex = reader.currentIndex();
        if (!force && wordIndex == lastSavedWordIndex)
            return;
        lastSaveMs = nowMs;
        cache(preferences, store, reader, static_cast<uint32_t>(wordIndex));
        Serial.printf("[storage-progress] saved position word=%u path=%s\n", static_cast<unsigned int>(wordIndex),
                      path.c_str());
    }

    void Session::cache(Preferences& preferences, const IndexedBookStore& store, const ReadingLoop& reader,
                        uint32_t wordIndex) {
        if (!fromStorage || path.isEmpty())
            return;
        const size_t wordCount = reader.wordCount();
        if (wordCount > 0)
            wordIndex = std::min<uint32_t>(wordIndex, static_cast<uint32_t>(wordCount - 1));
        settings::save<settings::prefs::BookPath>(preferences, path);
        preferences.putUInt(positionKey(path).c_str(), wordIndex);
        preferences.putUInt(wordCountKey(path).c_str(), static_cast<uint32_t>(wordCount));
        if (store.isOpen()) {
            preferences.putUInt(sourceSizeKey(path).c_str(), store.sourceSize());
            preferences.putUInt(sourceFingerprintKey(path).c_str(), store.sourceFingerprint());
        }
        settings::save<settings::prefs::Wpm>(preferences, reader.wpm());
        lastSavedWordIndex = wordIndex;
    }

    void Session::mirror(const IndexedBookStore& store, const ReadingLoop& reader) const {
        writeSessionSidecar(*this, store, static_cast<uint32_t>(reader.currentIndex()),
                            static_cast<uint32_t>(reader.wordCount()));
    }

    uint32_t Session::restore(Preferences& preferences, const IndexedBookStore& store, const ReadingLoop& reader) {
        uint32_t wordIndex = kNoSavedWordIndex;
        if (readSessionSidecar(*this, store, reader, wordIndex))
            return wordIndex;

        const String savedPositionKey = positionKey(path);
        if (!preferences.isKey(savedPositionKey.c_str()))
            return kNoSavedWordIndex;
        const String savedCountKey = wordCountKey(path);
        if (preferences.isKey(savedCountKey.c_str())
            && preferences.getUInt(savedCountKey.c_str(), 0) != static_cast<uint32_t>(reader.wordCount())) {
            return kNoSavedWordIndex;
        }
        const String savedSizeKey = sourceSizeKey(path);
        const String savedFingerprintKey = sourceFingerprintKey(path);
        if (preferences.isKey(savedSizeKey.c_str()) && preferences.isKey(savedFingerprintKey.c_str())
            && (preferences.getUInt(savedSizeKey.c_str(), 0) != store.sourceSize()
                || preferences.getUInt(savedFingerprintKey.c_str(), 0) != store.sourceFingerprint())) {
            return kNoSavedWordIndex;
        }
        wordIndex = preferences.getUInt(savedPositionKey.c_str(), 0);
        writeSessionSidecar(*this, store, wordIndex, static_cast<uint32_t>(reader.wordCount()));
        return wordIndex;
    }

    bool Session::saveChapterTransition(Preferences& preferences, const IndexedBookStore& store, ReadingLoop& reader,
                                        size_t previousWordIndex, size_t currentWordIndex, uint32_t nowMs) {
        if (!fromStorage || path.isEmpty())
            return false;
        for (const ChapterMarker& chapter: metadata.chapters) {
            if (chapter.wordIndex == 0 || chapter.wordIndex <= previousWordIndex
                || chapter.wordIndex > currentWordIndex)
                continue;
            reader.seekTo(chapter.wordIndex);
            save(preferences, store, reader, true, nowMs);
            mirror(store, reader);
            return true;
        }
        return false;
    }

    String Session::title(const StorageManager& storage) const {
        if (!metadata.title.isEmpty())
            return metadata.title;
        return fromStorage ? storage.bookDisplayName(index) : String("Demo");
    }

} // namespace ReadingProgress
