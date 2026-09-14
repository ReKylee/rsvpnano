#pragma once

#include <FS.h>
#include "app/screens/Navigation.h"
#include "app/screens/Presentation.h"
#include "focus/FocusOrientation.h"
#include "focus/FocusSession.h"
#include "focus/FocusTimers.h"
#include "ui/Ui.h"
#include "ui/Layouts.h"

namespace screens {

    class FocusScreen {
    public:
        void begin();
        void begin(fs::FS& filesystem);
        bool update(uint32_t nowMs);
        Action draw(ui::Context& ui, uint32_t nowMs, Screen& screen);
        void setTimers(focus::Timers timers);
        const focus::Timers& timers() const {
            return timers_;
        }
        void close();

    private:
        Action drawTimers(ui::Context& ui, Screen& screen);
        void drawEditor(ui::Context& ui, Screen& screen);
        void drawNameEditor(ui::Context& ui, Screen& screen);
        bool drawSession(ui::Context& ui, uint32_t nowMs);
        void edit(size_t index, bool creating, Screen& screen);
        bool persist(const focus::Timers& timers);

        fs::FS* filesystem_ = nullptr;
        focus::Timers timers_;
        focus::Timer draft_;
        focus::Session session_;
        focus::OrientationReader orientation_;
        ui::KeyboardState keyboard_;
        size_t editIndex_ = 0;
        size_t activeIndex_ = 0;
        bool creating_ = false;
        bool deleteConfirm_ = false;
        bool writable_ = false;
#if RSVP_UI_WATCH
        size_t selectedIndex_ = 0;
        ui::CarouselGesture carouselGesture_;
#endif
    };

} // namespace screens
