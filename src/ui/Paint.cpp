#include "ui/Ui.h"

namespace ui {
    Rect Context::paintBounds(Rect rect) const {
        if constexpr (displayWriteAlignment() == 1)
            return rect;
        const int x1 = rect.x & ~1;
        const int y1 = rect.y & ~1;
        const int x2 = (rect.x + std::max<int16_t>(0, rect.w)) & ~1;
        const int y2 = (rect.y + std::max<int16_t>(0, rect.h)) & ~1;
        // Snap shared boundaries alike. Clip transfers later so offscreen content does not reflow.
        return {static_cast<int16_t>(x1), static_cast<int16_t>(y1), static_cast<int16_t>(std::max(0, x2 - x1)),
                static_cast<int16_t>(std::max(0, y2 - y1))};
    }

    Arduino_Canvas* Context::paintBuffer() {
#ifdef RSVP_BOARD_CONFIG_HEADER
        if constexpr (displayWriteAlignment() > 1) {
            // aligned_alloc used by Arduino_Canvas requires a multiple-of-16 allocation size.
            constexpr int16_t pitch = (Board::Config::PANEL_NATIVE_WIDTH + 3) & ~3;
            constexpr int16_t rows = Board::Config::DISPLAY_BUFFER_ROWS;
            static_assert(displayWriteAlignment() == 1 || (rows > 0 && rows % displayWriteAlignment() == 0));
            static Arduino_Canvas buffer(pitch, rows, nullptr);
            if (gfx_.width() <= pitch && buffer.begin(GFX_SKIP_OUTPUT_BEGIN))
                return &buffer;
        }
#endif
        return nullptr;
    }

    void Context::clear(Rect rect) {
        if (rect.w <= 0 || rect.h <= 0) {
            return;
        }
        paint(rect, [](Arduino_GFX&, Rect) {});
    }

} // namespace ui
