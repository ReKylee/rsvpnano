#pragma once

#include <Arduino.h>

#include "input/Input.h"
#include "ui/Touch.h"

namespace Board::Input {

    bool begin();
    void end();
    void cancel();
    ::Input::ControlTiming controlTiming();
    ::Input::PressActions currentActions();
    ui::TouchSurface touchSurface();
    ui::TouchTiming touchTiming();
    bool beginTouch();
    bool touchReady();
    bool readTouch(ui::TouchContact& contact);

} // namespace Board::Input
