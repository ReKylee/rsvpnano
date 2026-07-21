#include "ui/screens/LibraryScreen.h"

#include <algorithm>
#include <climits>
#include <cstdlib>

#include "storage/index/IndexedBook.h"
#include "ui/screens/ScreenCommon.h"

namespace screens {
    namespace {

        constexpr int16_t kShelfHeight = 118;
        constexpr int16_t kDetailGap = 2;
        constexpr int16_t kGap = 5;

        uint16_t spineColor(size_t index, bool article) {
            constexpr uint16_t books[] = {0x99E3, 0x1AF5, 0x0B6A, 0x7B98, 0x4490, 0xB4CD, 0x9A49, 0x32FA};
            constexpr uint16_t articles[] = {0x8B88, 0x63CF, 0x82A9, 0x536A, 0x6ACF};
            return article ? articles[index % 5] : books[index % 8];
        }

    } // namespace

    LibraryResult LibraryScreen::draw(ui::Context& ui, const std::vector<LibraryItem>& items, uint32_t nowMs,
                                      Screen& screen) {
        LibraryResult result;
        const ui::Touch* touch = ui.touch();
        selectedIndex_ = items.empty() ? 0 : std::min(selectedIndex_, items.size() - 1);
        if (const Action action = detail::navigation(ui, Screen::Library, screen); action != Action::None) {
            result.action = action;
        }

        if (ui.width() < 620 || ui.height() < 150 || ui.height() > 240) {
            ui::Column column{detail::tabContent(ui), 4};
            const size_t visible = std::min<size_t>(items.size(), std::max<int16_t>(1, column.bounds.h / 24));
            for (size_t index = 0; index < visible; ++index) {
                if (ui.button(column.next(20), items[index].title)) {
                    result.open = index == selectedIndex_;
                    selectedIndex_ = index;
                }
            }
            return result;
        }

        const ui::Rect content = detail::tabContent(ui);
        const ui::Rect viewport{content.x, content.y, content.w,
                                std::min<int16_t>(kShelfHeight, static_cast<int16_t>(content.h - 36))};
        const int16_t detailY = static_cast<int16_t>(viewport.y + viewport.h + kDetailGap);
        const ui::Rect detailRect{content.x, detailY, content.w,
                                  std::max<int16_t>(0, static_cast<int16_t>(content.y + content.h - detailY))};
        if (!dragging_)
            offset_ = centeredOffset(items, selectedIndex_, viewport.w);

        if (touch != nullptr && ui::hasTouch(*touch, ui::TouchStart) && ui::contains(viewport, touch->x, touch->y)) {
            dragging_ = true;
            moved_ = false;
            startX_ = touch->x;
            startY_ = touch->y;
            startOffset_ = offset_;
            lastDrawMs_ = 0;
        }
        if (dragging_ && touch != nullptr && ui::hasTouch(*touch, ui::TouchMove)) {
            const int dx = static_cast<int>(touch->x) - startX_;
            const int dy = static_cast<int>(touch->y) - startY_;
            moved_ = moved_ || std::abs(dx) > 5 || std::abs(dy) > 5;
            if (lastDrawMs_ == 0 || nowMs - lastDrawMs_ >= 33) {
                offset_ = clampOffset(items, static_cast<int16_t>(startOffset_ + dx), viewport.w);
                selectedIndex_ = nearest(items, offset_, static_cast<int16_t>(viewport.x + viewport.w / 2), viewport.x);
                lastDrawMs_ = nowMs;
            }
        }
        if (dragging_ && touch != nullptr && ui::hasTouch(*touch, ui::TouchRelease)) {
            const size_t target =
                nearest(items, offset_, static_cast<int16_t>(viewport.x + viewport.w / 2), viewport.x);
            if (moved_) {
                selectedIndex_ = target;
                offset_ = centeredOffset(items, target, viewport.w);
            } else if (!items.empty() && ui::hasTouch(*touch, ui::TouchTap)
                       && ui::contains(viewport, touch->x, touch->y)) {
                const size_t tapped = spineAt(items, offset_, viewport, touch->x, touch->y);
                if (tapped == items.size()) {
                    result.open = true;
                } else {
                    result.open = tapped == selectedIndex_;
                    selectedIndex_ = tapped;
                    offset_ = centeredOffset(items, tapped, viewport.w);
                }
            }
            dragging_ = false;
        }
        if (touch != nullptr && ui::hasTouch(*touch, ui::TouchTap) && ui::contains(detailRect, touch->x, touch->y)) {
            result.open = true;
        }

        if (items.empty()) {
            ui.label(viewport, ui.text(UiText::NoLibraryItems), 2, ui::themes::ColorRole::Muted, ui::TextAlign::Center);
            return result;
        }

        const bool redrawShelf = ui.redraw({viewport.x, static_cast<int16_t>(viewport.y - 2), viewport.w,
                                            static_cast<int16_t>(viewport.h + 4)},
                                           signature(items, selectedIndex_));
        if (redrawShelf) {
            Arduino_GFX& gfx = ui.gfx();
            const uint16_t foreground = ui.color(ui::themes::ColorRole::Foreground);
            const uint16_t accent = ui.color(ui::themes::ColorRole::Accent);
            const uint16_t outline = ui.color(ui::themes::ColorRole::Outline);
            const int16_t marker = static_cast<int16_t>(viewport.x + viewport.w / 2);
            gfx.drawFastVLine(marker, viewport.y, viewport.h, ui.color(ui::themes::ColorRole::ProgressTrack));

            int16_t left = 0;
            for (size_t index = 0; index < items.size(); ++index) {
                const int16_t width = spineWidth(items[index], index);
                const int16_t height = spineHeight(items[index], index);
                const int16_t x = static_cast<int16_t>(viewport.x + left + offset_);
                left = static_cast<int16_t>(left + width + kGap);
                if (x + width < viewport.x || x > viewport.x + viewport.w)
                    continue;
                const bool active = index == selectedIndex_;
                const int16_t y = static_cast<int16_t>(viewport.y + viewport.h - height - (active ? 8 : 0));
                const uint16_t fill = spineColor(index, items[index].article);
                gfx.fillRect(x, y, width, height, fill);
                gfx.drawRect(x, y, width, height, foreground);
                if (active)
                    gfx.fillRect(x, static_cast<int16_t>(y - 2), width, 2, accent);
                if (items[index].progress > 0) {
                    const int16_t ribbonX = static_cast<int16_t>(x + width - 9);
                    const int16_t ribbonHeight =
                        std::max<int16_t>(8, static_cast<int16_t>(height * items[index].progress / 100));
                    gfx.fillRect(ribbonX, y, 5, ribbonHeight, 0xDACA);
                    for (int16_t row = 0; row < 3; ++row)
                        gfx.drawFastHLine(static_cast<int16_t>(ribbonX + 2 - row),
                                          static_cast<int16_t>(y + ribbonHeight - 3 + row),
                                          static_cast<int16_t>(row * 2 + 1), fill);
                }
                gfx.setTextSize(1);
                gfx.setTextColor(0xFF9C);
                int16_t textY = static_cast<int16_t>(y + 6);
                for (size_t character = 0; character < items[index].spineLabel.length() && textY + 8 < y + height;
                     ++character, textY = static_cast<int16_t>(textY + 11)) {
                    gfx.setCursor(static_cast<int16_t>(x + width / 2 - 3), textY);
                    gfx.write(static_cast<uint8_t>(items[index].spineLabel[character]));
                }
            }
            gfx.drawFastHLine(viewport.x, static_cast<int16_t>(viewport.y + viewport.h), viewport.w, outline);
            gfx.drawFastHLine(viewport.x, static_cast<int16_t>(viewport.y + viewport.h + 1), viewport.w, outline);
        }

        constexpr int16_t progressWidth = 62;
        constexpr int16_t detailGap = 12;
        const int16_t textWidth = static_cast<int16_t>(detailRect.w - progressWidth - detailGap);
        const LibraryItem& item = items[selectedIndex_];
        const std::string_view author = item.author.empty() ? ui.text(UiText::Unknown) : item.author;
        ui.label({detailRect.x, detailRect.y, textWidth, 16}, item.title, 2);
        ui.label({detailRect.x, static_cast<int16_t>(detailRect.y + 18), detailRect.w, 8}, author, 1,
                 ui::themes::ColorRole::Muted);
        ui.label({detailRect.x, static_cast<int16_t>(detailRect.y + 28), detailRect.w, 8}, item.chapter, 1,
                 ui::themes::ColorRole::Muted);
        ui.label({static_cast<int16_t>(detailRect.x + detailRect.w - progressWidth), detailRect.y, progressWidth, 16},
                 item.progressLabel, 2, ui::themes::ColorRole::Accent, ui::TextAlign::Right);
        return result;
    }

    void LibraryScreen::reset() {
        dragging_ = false;
        moved_ = false;
        offset_ = 0;
        lastDrawMs_ = 0;
    }

    void LibraryScreen::invalidate() {
        items_.clear();
        sourceCount_ = 0;
        itemsValid_ = false;
    }

    const std::vector<LibraryItem>& LibraryScreen::items(StorageManager& storage, const IndexedBookStore& bookStore,
                                                         const ReadingSession& session) {
        const size_t bookCount = storage.bookCount();
        if (itemsValid_ && sourceCount_ == bookCount) {
            if (session.fromStorage && session.bookIndex < items_.size() && bookStore.isOpen()) {
                LibraryItem& current = items_[session.bookIndex];
                current.progress = ReadingProgress::percent(session.currentIndex, ReadingLoop::wordCount(session));
                if (const ChapterMarker* chapter = session.metadata.chapterAt(session.currentIndex)) {
                    current.chapter = chapter->title;
                }
                current.progressLabel = progressLabel(current.progress);
            }
            return items_;
        }

        items_.clear();
        items_.reserve(bookCount);
        for (size_t index = 0; index < bookCount; ++index) {
            LibraryItem item;
            item.title = storage.bookDisplayName(index);
            item.author = storage.bookAuthorName(index);
            item.article = storage.bookIsArticle(index);
            item.spineLabel = spineLabel(item.title);

            const std::string path = storage.bookPath(index);
            BookMetadata metadata;
            IndexedBookStore::Header header;
            const bool metadataLoaded = IndexedBook::readMetadata(path.c_str(), metadata, &header);
            uint32_t wordIndex = 0;
            bool hasPosition = false;

            if (session.fromStorage && index == session.bookIndex && bookStore.isOpen()) {
                wordIndex = static_cast<uint32_t>(session.currentIndex);
                item.progress = ReadingProgress::percent(wordIndex, ReadingLoop::wordCount(session));
                if (const ChapterMarker* chapter = session.metadata.chapterAt(wordIndex))
                    item.chapter = chapter->title;
            } else if (metadataLoaded && header.wordCount > 0) {
                const ReadingProgress::BookIdentity identity{header.sourceSize, header.sourceFingerprint,
                                                             header.wordCount};
                const auto savedWordIndex = ReadingProgress::readBookStatePosition(path.c_str(), identity);
                hasPosition = savedWordIndex.has_value();
                wordIndex = savedWordIndex.value_or(0);
                item.progress = hasPosition ? ReadingProgress::percent(wordIndex, header.wordCount) : 0;
                if (const ChapterMarker* chapter = metadata.chapterAt(hasPosition ? wordIndex : 0)) {
                    item.chapter = chapter->title;
                }
            } else {
                item.progress = 0;
            }

            item.progressLabel = progressLabel(item.progress);
            items_.push_back(item);
        }
        sourceCount_ = bookCount;
        itemsValid_ = true;
        return items_;
    }

    std::string LibraryScreen::spineLabel(std::string_view title) {
        while (!title.empty()
               && (title.front() == ' ' || title.front() == '\t' || title.front() == '\r' || title.front() == '\n'))
            title.remove_prefix(1);
        while (!title.empty()
               && (title.back() == ' ' || title.back() == '\t' || title.back() == '\r' || title.back() == '\n'))
            title.remove_suffix(1);
        const auto startsWith = [title](std::string_view prefix) {
            if (title.size() < prefix.size())
                return false;
            for (size_t index = 0; index < prefix.size(); ++index) {
                const char character = title[index] >= 'A' && title[index] <= 'Z'
                                         ? static_cast<char>(title[index] - 'A' + 'a')
                                         : title[index];
                if (character != prefix[index])
                    return false;
            }
            return true;
        };
        if (startsWith("the "))
            title.remove_prefix(4);
        else if (startsWith("an "))
            title.remove_prefix(3);
        else if (startsWith("a "))
            title.remove_prefix(2);

        std::string result;
        result.reserve(7);
        for (char character: title) {
            if (result.size() == 7)
                break;
            if (character >= 'a' && character <= 'z')
                character = static_cast<char>(character - 'a' + 'A');
            if ((character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9'))
                result.push_back(character);
        }
        return result.empty() ? std::string{"BOOK"} : result;
    }

    std::string LibraryScreen::progressLabel(uint8_t progress) {
        if (progress == 0)
            return "0%";
        if (progress >= 100)
            return "100%";
        std::string result;
        result.reserve(3);
        if (progress >= 10)
            result.push_back(static_cast<char>('0' + progress / 10));
        result.push_back(static_cast<char>('0' + progress % 10));
        result.push_back('%');
        return result;
    }

    int16_t LibraryScreen::centeredOffset(const std::vector<LibraryItem>& items, size_t index,
                                          int16_t viewportWidth) const {
        if (items.empty())
            return 0;
        index = std::min(index, items.size() - 1);
        int16_t left = 0;
        for (size_t item = 0; item < index; ++item)
            left = static_cast<int16_t>(left + spineWidth(items[item], item) + kGap);
        return clampOffset(items, static_cast<int16_t>(viewportWidth / 2 - left - spineWidth(items[index], index) / 2),
                           viewportWidth);
    }

    int16_t LibraryScreen::clampOffset(const std::vector<LibraryItem>& items, int16_t offset,
                                       int16_t viewportWidth) const {
        if (items.empty())
            return 0;
        int16_t left = 0, lastCenter = 0;
        for (size_t index = 0; index < items.size(); ++index) {
            const int16_t width = spineWidth(items[index], index);
            lastCenter = static_cast<int16_t>(left + width / 2);
            left = static_cast<int16_t>(left + width + kGap);
        }
        const int16_t firstCenter = spineWidth(items[0], 0) / 2;
        return std::clamp<int16_t>(offset, static_cast<int16_t>(viewportWidth / 2 - lastCenter),
                                   static_cast<int16_t>(viewportWidth / 2 - firstCenter));
    }

    size_t LibraryScreen::nearest(const std::vector<LibraryItem>& items, int16_t offset, int16_t x,
                                  int16_t viewportX) const {
        if (items.empty())
            return 0;
        size_t result = 0;
        int distance = INT_MAX;
        int16_t left = 0;
        for (size_t index = 0; index < items.size(); ++index) {
            const int16_t width = spineWidth(items[index], index);
            const int center = viewportX + left + width / 2 + offset;
            if (std::abs(center - x) < distance) {
                distance = std::abs(center - x);
                result = index;
            }
            left = static_cast<int16_t>(left + width + kGap);
        }
        return result;
    }

    size_t LibraryScreen::spineAt(const std::vector<LibraryItem>& items, int16_t offset, const ui::Rect& viewport,
                                  uint16_t x, uint16_t y) const {
        int16_t left = 0;
        for (size_t index = 0; index < items.size(); ++index) {
            const int16_t width = spineWidth(items[index], index);
            const int16_t height = spineHeight(items[index], index);
            const int16_t spineX = static_cast<int16_t>(viewport.x + left + offset);
            const int16_t spineY =
                static_cast<int16_t>(viewport.y + viewport.h - height - (index == selectedIndex_ ? 8 : 0));
            if (ui::contains({spineX, spineY, width, height}, x, y))
                return index;
            left = static_cast<int16_t>(left + width + kGap);
        }
        return items.size();
    }

    int16_t LibraryScreen::spineWidth(const LibraryItem& item, size_t index) const {
        return static_cast<int16_t>((item.article ? 22 : 24) + std::min<size_t>(item.title.length(), 18) / 3
                                    + (index * 7) % 13);
    }

    int16_t LibraryScreen::spineHeight(const LibraryItem& item, size_t index) const {
        return std::min<int16_t>(110, static_cast<int16_t>((item.article ? 78 : 84)
                                                           + std::min<size_t>(item.title.length(), 24) / 2
                                                           + (index * 5) % 17));
    }

    uint32_t LibraryScreen::signature(const std::vector<LibraryItem>& items, size_t current) const {
        uint32_t value = ui::Context::combine(static_cast<uint32_t>(offset_), static_cast<uint32_t>(current));
        value = ui::Context::combine(value, items.size());
        for (const LibraryItem& item: items) {
            value = ui::Context::signature(item.title, value);
            value = ui::Context::combine(value, item.progress);
        }
        return value;
    }

} // namespace screens
