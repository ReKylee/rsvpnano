#pragma once

#include <vector>

#include <Preferences.h>

#include "reader/ReadingLoop.h"
#include "storage/StorageManager.h"
#include "storage/index/IndexedBookStore.h"
#include "storage/index/ReadingProgress.h"
#include "ui/screens/Screens.h"

namespace screens {

    struct LibraryItem {
        String title;
        String author;
        String chapter;
        String detail;
        String progressLabel;
        String spineLabel;
        uint8_t progress = 0;
        bool article = false;
    };

    struct LibraryResult {
        Action action = Action::None;
        bool open = false;
    };

    class LibraryScreen {
    public:
        LibraryResult draw(ui::Context& ui, const std::vector<LibraryItem>& items, uint32_t nowMs, Screen& screen);
        void reset();
        void invalidate();
        const std::vector<LibraryItem>& items(StorageManager& storage, const IndexedBookStore& bookStore,
                                              const ReadingLoop& reader, const ReadingProgress::Session& book,
                                              Preferences& preferences);
        size_t selectedIndex() const {
            return selectedIndex_;
        }

    private:
        int16_t centeredOffset(const std::vector<LibraryItem>& items, size_t index) const;
        int16_t clampOffset(const std::vector<LibraryItem>& items, int16_t offset) const;
        size_t nearest(const std::vector<LibraryItem>& items, int16_t offset, int16_t x) const;
        int16_t spineWidth(const LibraryItem& item, size_t index) const;
        int16_t spineHeight(const LibraryItem& item, size_t index) const;
        uint32_t signature(const std::vector<LibraryItem>& items, size_t current) const;
        uint8_t storedProgress(size_t index, StorageManager& storage, const IndexedBookStore& bookStore,
                               const ReadingLoop& reader, const ReadingProgress::Session& book,
                               Preferences& preferences) const;
        static String spineLabel(const String& title);
        static String detailLine(const String& author, const String& chapter);
        static String progressLabel(uint8_t progress);

        bool dragging_ = false;
        bool moved_ = false;
        uint16_t startX_ = 0;
        uint16_t startY_ = 0;
        int16_t startOffset_ = 0;
        int16_t offset_ = 0;
        uint32_t lastDrawMs_ = 0;
        size_t selectedIndex_ = 0;
        std::vector<LibraryItem> items_;
        size_t sourceCount_ = 0;
        bool itemsValid_ = false;
    };

} // namespace screens
