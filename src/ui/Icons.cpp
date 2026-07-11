#include "ui/Ui.h"

#include <algorithm>

namespace ui {

    void Context::drawIcon(Rect rect, Icon icon, uint16_t ink, uint16_t surface) {
        switch (icon) {
        case Icon::Bookmark:
            drawBookmarkIcon(rect, ink, surface);
            break;
        case Icon::Books:
            drawBooksIcon(rect, ink);
            break;
        case Icon::Edit:
            drawEditIcon(rect, ink);
            break;
        case Icon::Device:
            drawDeviceIcon(rect, ink);
            break;
        case Icon::Hourglass:
            drawHourglassIcon(rect, ink);
            break;
        case Icon::Power:
            drawPowerIcon(rect, ink, surface);
            break;
        default:
            break;
        }
    }

    void Context::drawBookmarkIcon(Rect rect, uint16_t ink, uint16_t surface) {
        const int16_t cx = static_cast<int16_t>(rect.x + rect.w / 2);
        const int16_t width = std::max<int16_t>(1, std::min<int16_t>(13, static_cast<int16_t>(rect.w - 6)));
        const int16_t height = std::max<int16_t>(1, std::min<int16_t>(30, static_cast<int16_t>(rect.h - 4)));
        const int16_t x = static_cast<int16_t>(cx - width / 2);
        const int16_t y = static_cast<int16_t>(rect.y + 1);
        gfx_.fillRect(x, y, width, height, ink);
        for (int16_t row = 0; row <= std::min<int16_t>(6, height - 1); ++row) {
            const int16_t half = std::min<int16_t>(row, width / 2);
            gfx_.drawFastHLine(static_cast<int16_t>(cx - half), static_cast<int16_t>(y + height - 7 + row),
                               static_cast<int16_t>(half * 2 + 1), surface);
        }
    }

    void Context::drawBooksIcon(Rect rect, uint16_t ink) {
        const int16_t x = static_cast<int16_t>(rect.x + rect.w / 2 - 9);
        const int16_t y = static_cast<int16_t>(rect.y + rect.h / 2 - 9);
        gfx_.drawRect(x, y, 5, 18, ink);
        gfx_.drawRect(static_cast<int16_t>(x + 6), static_cast<int16_t>(y + 2), 5, 16, ink);
        gfx_.drawRect(static_cast<int16_t>(x + 12), static_cast<int16_t>(y - 1), 6, 19, ink);
    }

    void Context::drawEditIcon(Rect rect, uint16_t ink) {
        const int16_t x = static_cast<int16_t>(rect.x + rect.w / 2 - 9);
        const int16_t y = static_cast<int16_t>(rect.y + rect.h / 2 - 9);
        gfx_.drawRect(x, y, 14, 18, ink);
        gfx_.drawLine(static_cast<int16_t>(x + 5), static_cast<int16_t>(y + 13), static_cast<int16_t>(x + 18), y, ink);
        gfx_.drawLine(static_cast<int16_t>(x + 6), static_cast<int16_t>(y + 16), static_cast<int16_t>(x + 19),
                      static_cast<int16_t>(y + 3), ink);
    }

    void Context::drawDeviceIcon(Rect rect, uint16_t ink) {
        const int16_t cx = static_cast<int16_t>(rect.x + rect.w / 2);
        const int16_t x = static_cast<int16_t>(cx - 8);
        const int16_t y = static_cast<int16_t>(rect.y + rect.h / 2 - 10);
        gfx_.drawRoundRect(x, y, 16, 20, 3, ink);
        gfx_.fillRect(static_cast<int16_t>(cx - 2), static_cast<int16_t>(y + 16), 4, 1, ink);
    }

    void Context::drawHourglassIcon(Rect rect, uint16_t ink) {
        const int16_t cx = static_cast<int16_t>(rect.x + rect.w / 2);
        const int16_t cy = static_cast<int16_t>(rect.y + rect.h / 2);
        const int16_t x = static_cast<int16_t>(cx - 8);
        const int16_t y = static_cast<int16_t>(cy - 9);
        gfx_.drawLine(x, y, static_cast<int16_t>(x + 16), y, ink);
        gfx_.drawLine(x, static_cast<int16_t>(y + 18), static_cast<int16_t>(x + 16), static_cast<int16_t>(y + 18), ink);
        gfx_.drawLine(static_cast<int16_t>(x + 2), static_cast<int16_t>(y + 1), static_cast<int16_t>(x + 14),
                      static_cast<int16_t>(y + 17), ink);
        gfx_.drawLine(static_cast<int16_t>(x + 14), static_cast<int16_t>(y + 1), static_cast<int16_t>(x + 2),
                      static_cast<int16_t>(y + 17), ink);
        gfx_.fillRect(static_cast<int16_t>(cx - 3), static_cast<int16_t>(cy + 5), 6, 2, ink);
    }

    void Context::drawPowerIcon(Rect rect, uint16_t ink, uint16_t surface) {
        const int16_t cx = static_cast<int16_t>(rect.x + rect.w / 2);
        const int16_t cy = static_cast<int16_t>(rect.y + rect.h / 2);
        gfx_.drawCircle(cx, static_cast<int16_t>(cy + 1), 8, ink);
        gfx_.fillRect(static_cast<int16_t>(cx - 3), static_cast<int16_t>(cy - 9), 7, 10, surface);
        gfx_.drawFastVLine(cx, static_cast<int16_t>(cy - 9), 9, ink);
    }

} // namespace ui
