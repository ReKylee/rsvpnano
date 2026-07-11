#include "ui/screens/StandbyScreen.h"

#include <algorithm>

#include "standby/PackedGrid.h"
#include "ui/screens/Screens.h"

namespace screens {

    namespace {
        constexpr uint8_t kCellSize = 4;
        constexpr uint32_t kFrameMs = 160;

        constexpr uint16_t ceilDiv(uint16_t numerator, uint16_t denominator) {
            return static_cast<uint16_t>((numerator + denominator - 1U) / denominator);
        }
    } // namespace

    void StandbyScreen::begin(ui::Context& ui, uint32_t nowMs, size_t bookIndex, size_t wordIndex,
                              standby::Kind kind) {
        kind_ = kind;
        columns_ =
            std::clamp<uint16_t>(ceilDiv(std::max<int16_t>(1, ui.width()), kCellSize), 1, standby::kMaxStandbyColumns);
        rows_ =
            std::clamp<uint16_t>(ceilDiv(std::max<int16_t>(1, ui.height()), kCellSize), 1, standby::kMaxStandbyRows);
        screensaver_.select(kind, columns_, rows_);
        const uint32_t seed =
            nowMs ^ (static_cast<uint32_t>(bookIndex) << 16U) ^ (static_cast<uint32_t>(wordIndex) * 2654435761UL);
        screensaver_.seed(seed == 0 ? 1U : seed);
        nextFrameMs_ = nowMs;
    }

    void StandbyScreen::reset() {
        screensaver_.reset();
    }

    void StandbyScreen::update(ui::Context& ui, uint32_t nowMs) {
        if (!screensaver_)
            begin(ui, nowMs, 0, 0, kind_);
        if (static_cast<int32_t>(nowMs - nextFrameMs_) < 0)
            return;
        uint8_t steps = 0;
        do {
            screensaver_.step();
            nextFrameMs_ += kFrameMs;
            ++steps;
        } while (steps < 3 && static_cast<int32_t>(nowMs - nextFrameMs_) >= 0);
        draw(ui);
    }

    void StandbyScreen::draw(ui::Context& ui) {
        if (!screensaver_)
            return;
        const standby::Frame frame = screensaver_.frame();
        ui.beginFrame(static_cast<uint8_t>(Screen::Standby));
        uint32_t state = ui::Context::combine(frame.generation, frame.fullRedraw);
        state = ui::Context::combine(state, columns_);
        state = ui::Context::combine(state, rows_);
        state = ui::Context::combine(state, kCellSize);
        if (!ui.redraw({0, 0, ui.width(), ui.height()}, state)) {
            ui.endFrame();
            return;
        }
        if (!frame.cells.valid() || columns_ == 0 || rows_ == 0) {
            ui.endFrame();
            return;
        }

        Arduino_GFX& gfx = ui.gfx();
        const int16_t originX = static_cast<int16_t>((ui.width() - columns_ * kCellSize) / 2);
        const int16_t originY = static_cast<int16_t>((ui.height() - rows_ * kCellSize) / 2);
        const uint16_t dim = ui.blend(ui::themes::ColorRole::Foreground, 72);
        for (uint16_t row = 0; row < rows_; ++row) {
            for (uint16_t column = 0; column < columns_; ++column) {
                const size_t index = static_cast<size_t>(row) * columns_ + column;
                const uint16_t color = standby::cellAlive(frame.cells, index)
                                         ? ui.color(ui::themes::ColorRole::Foreground)
                                     : frame.dimCells.valid() && standby::cellAlive(frame.dimCells, index)
                                         ? dim
                                         : ui.color(ui::themes::ColorRole::Background);
                gfx.fillRect(static_cast<int16_t>(originX + column * kCellSize),
                             static_cast<int16_t>(originY + row * kCellSize), kCellSize, kCellSize, color);
            }
        }
        ui.endFrame();
    }

} // namespace screens
