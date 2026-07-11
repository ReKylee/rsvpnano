#pragma once

#include "standby/Screensaver.h"
#include "ui/Ui.h"

namespace screens {

class StandbyScreen {
public:
    void begin(ui::Context& ui, uint32_t nowMs, size_t bookIndex, size_t wordIndex);
    void reset();
    void update(ui::Context& ui, uint32_t nowMs);
    void draw(ui::Context& ui);

private:
    standby::ScreensaverSlot screensaver_;
    uint32_t nextFrameMs_ = 0;
    uint16_t columns_ = 0;
    uint16_t rows_ = 0;
};

} // namespace screens
