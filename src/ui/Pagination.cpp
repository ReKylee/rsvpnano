#include "ui/Ui.h"
#include "ui/Layouts.h"

namespace ui {
    PagedGrid Context::pagedGrid(Rect rect, size_t count, uint8_t columns, int16_t minimumHeight) {
        rect = paintBounds(rect);
        columns = std::max<uint8_t>(1, columns);
        constexpr int16_t gap = 4;
        int rows = std::max(1, (rect.h + gap) / (minimumHeight + gap));
        const bool paging = count > static_cast<size_t>(rows * columns);
        if (paging) {
            rect.h = std::max<int16_t>(1, rect.h - 36);
            rows = std::max(1, (rect.h + gap) / (minimumHeight + gap));
        } else {
            rows = std::min<size_t>(rows, std::max<size_t>(1, (count + columns - 1) / columns));
        }
        const size_t capacity = rows * columns;
        const size_t pages = std::max<size_t>(1, (count + capacity - 1) / capacity);
        gridPage_ = std::min(gridPage_, pages - 1);
        if (paging) {
            const int16_t y = rect.y + rect.h + 4;
            if (button({rect.x, y, 48, 32}, "<", gridPage_ > 0)) {
                --gridPage_;
                invalidate();
            }
            if (button({static_cast<int16_t>(rect.x + rect.w - 48), y, 48, 32}, ">", gridPage_ + 1 < pages)) {
                ++gridPage_;
                invalidate();
            }
            label({static_cast<int16_t>(rect.x + 52), y, static_cast<int16_t>(rect.w - 104), 32},
                  std::to_string(gridPage_ + 1) + "/" + std::to_string(pages), 2, themes::Muted, TextAlign::Center);
        }
        const size_t first = gridPage_ * capacity;
        return {rect,
                first,
                std::min(capacity, count - std::min(first, count)),
                columns,
                static_cast<int16_t>((rect.h - (rows - 1) * gap) / rows),
                gap};
    }

} // namespace ui
