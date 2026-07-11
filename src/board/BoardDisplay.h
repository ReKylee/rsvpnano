#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

#include "ui/Touch.h"

namespace Board::Display {

    bool begin();
    Arduino_GFX& gfx();
    void holdBacklightOffForDeepSleep();
    ui::Orientation defaultUiOrientation();
    ui::Orientation rotatedUiOrientation();
    uint16_t nativeWidth();
    uint16_t nativeHeight();
    size_t txChunkBytes();
    void flush();
    bool flushRegion(uint16_t x, uint16_t y, uint16_t width, uint16_t height);
    void setBacklight(bool on);
    void setBrightness(uint8_t percent);
    void sleep();
    void wake();
    bool pushColors(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint16_t* data);

} // namespace Board::Display
