#include "ui/screens/ScreenCommon.h"

#include <algorithm>
#include <cstdio>
#include <utility>

#include "timer/FocusTimerStorage.h"

namespace screens {
    void FocusScreen::begin(fs::FS* filesystem) {
        filesystem_ = filesystem;
        timers_.items[0] = focus::defaultTimer();
        timers_.count = 1;
        writable_ = filesystem_ != nullptr;
        if (filesystem_ != nullptr) {
            focus::Timers loaded;
            const focus::LoadResult result = focus::load(*filesystem_, loaded);
            if (result == focus::LoadResult::Valid) {
                timers_ = std::move(loaded);
            } else if (result == focus::LoadResult::Missing) {
                writable_ = focus::save(*filesystem_, timers_);
            } else {
                Serial.println("[focus] invalid focus.conf; using default timer");
            }
        }
        orientation_.begin();
    }

    bool FocusScreen::update(uint32_t nowMs) {
        const focus::Orientation orientation = orientation_.update(nowMs);
        session_.update(nowMs, orientation);
        return session_.consumeCompletionCue();
    }

    void FocusScreen::draw(ui::Context& ui, uint32_t nowMs, Screen& screen) {
        switch (screen) {
        case Screen::FocusTimers:
            drawTimers(ui, screen);
            break;
        case Screen::FocusEditor:
            drawEditor(ui, screen);
            break;
        case Screen::FocusNameEdit:
            drawNameEditor(ui, screen);
            break;
        case Screen::FocusSession:
            if (drawSession(ui, nowMs))
                screen = Screen::FocusTimers;
            break;
        default:
            break;
        }
    }

    void FocusScreen::close() {
        session_.stop();
    }

    void FocusScreen::drawTimers(ui::Context& ui, Screen& screen) {
        detail::navigation(ui, Screen::FocusTimers, screen);
        const ui::Rect content = detail::tabContent(ui);
        constexpr int16_t columns = 3;
        constexpr int16_t rows = 2;
        constexpr int16_t rowGap = 8;
        const int16_t cellWidth = static_cast<int16_t>(content.w / columns);
        const int16_t cellHeight = static_cast<int16_t>((content.h - rowGap) / rows);
        const size_t visibleCount = timers_.count + (timers_.count < focus::kMaxTimers ? 1 : 0);

        uint32_t state = static_cast<uint32_t>(timers_.count);
        state = ui::Context::combine(state, writable_);
        state = ui::Context::combine(state, orientation_.available());
        for (size_t index = 0; index < timers_.count; ++index) {
            const focus::Timer& timer = timers_.items[index];
            state = ui::Context::signature(timer.name, state);
            state = ui::Context::combine(state, timer.focusMinutes);
            state = ui::Context::combine(state, timer.breakMinutes);
            state = ui::Context::combine(state, timer.rounds);
        }
        const bool redraw = ui.redraw(content, state);

        for (size_t index = 0; index < visibleCount; ++index) {
            const ui::Rect cell{static_cast<int16_t>(content.x + (index % columns) * cellWidth),
                                static_cast<int16_t>(content.y + (index / columns) * (cellHeight + rowGap)),
                                cellWidth, cellHeight};

            if (index == timers_.count) {
                if (redraw) {
                    const uint16_t ink = ui.color(writable_ ? ui::themes::ColorRole::Accent
                                                            : ui::themes::ColorRole::Muted);
                    const int16_t centerX = static_cast<int16_t>(cell.x + cell.w / 2);
                    const int16_t centerY = static_cast<int16_t>(cell.y + cell.h / 2);
                    ui.gfx().drawCircle(centerX, centerY, 21, ink);
                    ui.gfx().fillRect(static_cast<int16_t>(centerX - 1), static_cast<int16_t>(centerY - 8), 3, 17,
                                      ink);
                    ui.gfx().fillRect(static_cast<int16_t>(centerX - 8), static_cast<int16_t>(centerY - 1), 17, 3,
                                      ink);
                }
                if (ui.tap(cell, writable_)) {
                    edit(timers_.count, true, screen);
                }
                continue;
            }

            const focus::Timer& timer = timers_.items[index];
            if (redraw) {
                const uint16_t ink = ui.color(orientation_.available() ? ui::themes::ColorRole::Foreground
                                                                       : ui::themes::ColorRole::Muted);
                const uint16_t outline = ui.color(ui::themes::ColorRole::Outline);
                constexpr int16_t glassWidth = 52;
                constexpr int16_t roundsWidth = 36;
                constexpr int16_t groupGap = 4;
                constexpr int16_t visualHeight = 70;
                constexpr int16_t chamberHeight = 18;
                constexpr int16_t taperHeight = 8;
                constexpr int16_t baseOverhang = 5;
                constexpr int16_t baseHeight = 2;
                constexpr int16_t textLift = 2;
                const int16_t visualTop = static_cast<int16_t>(cell.y + (cell.h - visualHeight) / 2);
                const int16_t centerX = static_cast<int16_t>(cell.x + cell.w / 2);
                const int16_t left = static_cast<int16_t>(centerX - glassWidth / 2);
                const int16_t right = static_cast<int16_t>(left + glassWidth - 1);
                const int16_t top = static_cast<int16_t>(visualTop + 18);
                const int16_t topRectBottom = static_cast<int16_t>(top + chamberHeight);
                const int16_t waist = static_cast<int16_t>(topRectBottom + taperHeight);
                const int16_t bottomRectTop = static_cast<int16_t>(waist + taperHeight);
                const uint16_t focusSand = ui.blend(ui::themes::ColorRole::Accent, 64);
                const uint16_t breakSand = ui.blend(ui::themes::ColorRole::BreakAccent, 64);
                const uint16_t base = ui.color(ui::themes::ColorRole::SurfaceActive);
                const int16_t titleWidth = static_cast<int16_t>(cell.w - 4);
                const uint8_t titleSize = timer.name.size() * 12 <= static_cast<size_t>(titleWidth) ? 2 : 1;
                ui.drawText({static_cast<int16_t>(cell.x + 2), static_cast<int16_t>(visualTop - textLift),
                             titleWidth, 16},
                            timer.name, titleSize, ink, ui::TextAlign::Center);
                ui.gfx().fillRect(left, top, glassWidth, chamberHeight, focusSand);
                ui.gfx().fillTriangle(left, topRectBottom, right, topRectBottom, centerX, waist, focusSand);
                ui.gfx().fillTriangle(centerX, waist, left, bottomRectTop, right, bottomRectTop, breakSand);
                ui.gfx().fillRect(left, bottomRectTop, glassWidth, chamberHeight, breakSand);
                ui.gfx().fillRect(static_cast<int16_t>(left - baseOverhang), static_cast<int16_t>(top - baseHeight),
                                  static_cast<int16_t>(glassWidth + baseOverhang * 2), baseHeight, base);
                ui.gfx().fillRect(static_cast<int16_t>(left - baseOverhang),
                                  static_cast<int16_t>(bottomRectTop + chamberHeight),
                                  static_cast<int16_t>(glassWidth + baseOverhang * 2), baseHeight, base);
                ui.gfx().drawFastHLine(left, top, glassWidth, outline);
                ui.gfx().drawFastVLine(left, top, chamberHeight, outline);
                ui.gfx().drawFastVLine(right, top, chamberHeight, outline);
                ui.gfx().drawLine(left, topRectBottom, centerX, waist, outline);
                ui.gfx().drawLine(right, topRectBottom, centerX, waist, outline);
                ui.gfx().drawLine(centerX, waist, left, bottomRectTop, outline);
                ui.gfx().drawLine(centerX, waist, right, bottomRectTop, outline);
                ui.gfx().drawFastVLine(left, bottomRectTop, chamberHeight, outline);
                ui.gfx().drawFastVLine(right, bottomRectTop, chamberHeight, outline);
                ui.gfx().drawFastHLine(left, static_cast<int16_t>(bottomRectTop + chamberHeight - 1), glassWidth,
                                       outline);
                char focusTime[8];
                char breakTime[8];
                char rounds[8];
                std::snprintf(focusTime, sizeof(focusTime), "%um", static_cast<unsigned int>(timer.focusMinutes));
                std::snprintf(breakTime, sizeof(breakTime), "%um", static_cast<unsigned int>(timer.breakMinutes));
                std::snprintf(rounds, sizeof(rounds), "x%u", static_cast<unsigned int>(timer.rounds));
                ui.drawText({left, top, glassWidth, chamberHeight}, focusTime, 2, ink, ui::TextAlign::Center);
                ui.drawText({left, static_cast<int16_t>(bottomRectTop - textLift), glassWidth, chamberHeight},
                            breakTime, 2, ink, ui::TextAlign::Center);
                ui.drawText({static_cast<int16_t>(right + groupGap),
                             static_cast<int16_t>(bottomRectTop - textLift), roundsWidth, chamberHeight},
                            rounds, 2, ink, ui::TextAlign::Center);
            }

            const bool tapped = ui.tap(cell, orientation_.available());
            const ui::Touch* touch = ui.touch();
            const bool held = writable_ && touch != nullptr && ui::hasTouch(*touch, ui::TouchHold)
                           && ui::contains(cell, touch->x, touch->y);
            if (held) {
                edit(index, false, screen);
            } else if (tapped) {
                activeIndex_ = index;
                session_.begin(timer);
                screen = Screen::FocusSession;
            }
        }

        if (redraw)
            ui.markDirty(content);
    }

    void FocusScreen::drawEditor(ui::Context& ui, Screen& screen) {
        const ui::Rect content = detail::content(ui);
        constexpr int16_t gap = 6;
        constexpr int16_t headerHeight = 24;
        constexpr int16_t saveWidth = 96;
        constexpr int16_t actionHeight = 34;
        if (ui.button({content.x, content.y, 64, 24}, ui.text(UiText::Back))) {
            deleteConfirm_ = false;
            screen = Screen::FocusTimers;
        }
        const int16_t saveX = static_cast<int16_t>(content.x + content.w - saveWidth);
        ui.label({static_cast<int16_t>(content.x + 74), content.y,
                  static_cast<int16_t>(saveX - content.x - 80), headerHeight},
                 ui.text(UiText::FocusTimer), 2);
        const bool save = ui.button({saveX, content.y, saveWidth, headerHeight},
                                    ui.text(creating_ ? UiText::Add : UiText::Save),
                                    writable_ && !deleteConfirm_ && focus::valid(draft_));

        const int16_t bodyY = static_cast<int16_t>(content.y + headerHeight + gap);
        const int16_t bodyHeight = static_cast<int16_t>(content.y + content.h - bodyY);
        const bool wide = content.w >= 480;
        ui::Rect name;
        ui::Rect focusSlider;
        ui::Rect breakSlider;
        ui::Rect roundsSlider;
        ui::Rect deleteAction;
        if (wide) {
            constexpr int16_t detailsWidth = 200;
            constexpr int16_t sliderGap = 4;
            const int16_t controlsX = static_cast<int16_t>(content.x + detailsWidth + gap);
            const int16_t controlsWidth = static_cast<int16_t>(content.w - detailsWidth - gap);
            const int16_t sliderHeight = static_cast<int16_t>((bodyHeight - sliderGap * 2) / 3);
            name = {content.x, bodyY, detailsWidth, 64};
            focusSlider = {controlsX, bodyY, controlsWidth, sliderHeight};
            breakSlider = {controlsX, static_cast<int16_t>(bodyY + sliderHeight + sliderGap), controlsWidth,
                           sliderHeight};
            roundsSlider = {controlsX, static_cast<int16_t>(bodyY + (sliderHeight + sliderGap) * 2), controlsWidth,
                            sliderHeight};
            deleteAction = {content.x, static_cast<int16_t>(content.y + content.h - actionHeight), detailsWidth,
                            actionHeight};
        } else {
            const int16_t sliderY = static_cast<int16_t>(bodyY + 40);
            const int16_t sliderWidth = static_cast<int16_t>((content.w - gap * 2) / 3);
            name = {content.x, bodyY, content.w, 34};
            focusSlider = {content.x, sliderY, sliderWidth, 50};
            breakSlider = {static_cast<int16_t>(content.x + sliderWidth + gap), sliderY, sliderWidth, 50};
            roundsSlider = {static_cast<int16_t>(content.x + (sliderWidth + gap) * 2), sliderY, sliderWidth, 50};
            deleteAction = {content.x, static_cast<int16_t>(content.y + content.h - 30), content.w, 30};
        }

        if (ui.setting(name, ui.text(UiText::TimerName), draft_.name)) {
            keyboard_ = {};
            screen = Screen::FocusNameEdit;
        }

        if (const auto value = ui.slider(focusSlider, ui.text(UiText::FocusMinutes), draft_.focusMinutes, 1, 180, 1,
                                         " min", ui::themes::ColorRole::Accent);
            value.changed)
            draft_.focusMinutes = static_cast<uint16_t>(value.value);
        if (const auto value = ui.slider(breakSlider, ui.text(UiText::BreakMinutes), draft_.breakMinutes, 1, 60, 1,
                                         " min", ui::themes::ColorRole::BreakAccent);
            value.changed)
            draft_.breakMinutes = static_cast<uint16_t>(value.value);
        if (const auto value = ui.slider(roundsSlider, ui.text(UiText::Rounds), draft_.rounds, 1, 12);
            value.changed)
            draft_.rounds = static_cast<uint8_t>(value.value);

        if (!creating_) {
            if (wide)
                ui.label({deleteAction.x, static_cast<int16_t>(deleteAction.y - 26), deleteAction.w, 20},
                         deleteConfirm_ ? ui.text(UiText::AreYouSure) : std::string_view{}, 2,
                         ui::themes::ColorRole::Muted, ui::TextAlign::Center);
            const int16_t actionWidth = static_cast<int16_t>((deleteAction.w - gap) / 2);
            ui::Row row{deleteAction, gap};
            const ui::Rect cancelAction = row.next(actionWidth);
            const ui::Rect confirmAction = row.next(actionWidth);
            if (deleteConfirm_) {
                if (ui.button(cancelAction, ui.text(UiText::Back)))
                    deleteConfirm_ = false;
            } else {
                ui.tap(cancelAction, false);
            }
            if (ui.button(confirmAction, ui.text(UiText::Delete), writable_ && timers_.count > 1)) {
                if (!deleteConfirm_) {
                    deleteConfirm_ = true;
                } else {
                    focus::Timers updated = timers_;
                    for (size_t index = editIndex_ + 1; index < updated.count; ++index)
                        updated.items[index - 1] = std::move(updated.items[index]);
                    --updated.count;
                    if (persist(updated)) {
                        timers_ = std::move(updated);
                        deleteConfirm_ = false;
                        screen = Screen::FocusTimers;
                    }
                }
            }
        }

        if (save) {
            focus::Timers updated = timers_;
            updated.items[editIndex_] = draft_;
            if (creating_)
                ++updated.count;
            if (persist(updated)) {
                timers_ = std::move(updated);
                screen = Screen::FocusTimers;
            }
        }
    }

    void FocusScreen::drawNameEditor(ui::Context& ui, Screen& screen) {
        const ui::Rect content = detail::content(ui);
        ui.label({content.x, content.y, content.w, 22}, ui.text(UiText::TimerName), 2,
                 ui::themes::ColorRole::Foreground, ui::TextAlign::Center);
        const ui::KeyboardAction action =
            ui.keyboard({content.x, static_cast<int16_t>(content.y + 24), content.w,
                         static_cast<int16_t>(content.h - 24)}, draft_.name, focus::kMaxTimerNameBytes, keyboard_);
        if (action == ui::KeyboardAction::Cancel || action == ui::KeyboardAction::Submit)
            screen = Screen::FocusEditor;
    }

    bool FocusScreen::drawSession(ui::Context& ui, uint32_t nowMs) {
        const ui::Rect area = detail::content(ui);
        const focus::Timer& timer = timers_.items[activeIndex_];
        const focus::Phase phase = session_.phase();
        const bool paused = phase == focus::Phase::PausedFocus || phase == focus::Phase::PausedBreak;
        const bool reversed = phase == focus::Phase::Break || phase == focus::Phase::PausedBreak;
        const bool focusPhase = phase == focus::Phase::WaitingFocus || phase == focus::Phase::Focus
                             || phase == focus::Phase::PausedFocus;
        const bool complete = phase == focus::Phase::Complete;
        uint32_t remaining = session_.remainingMs(nowMs);
        if (phase == focus::Phase::WaitingFocus)
            remaining = static_cast<uint32_t>(timer.focusMinutes) * 60UL * 1000UL;
        else if (phase == focus::Phase::WaitingBreak)
            remaining = static_cast<uint32_t>(timer.breakMinutes) * 60UL * 1000UL;
        const uint32_t seconds = (remaining + 999UL) / 1000UL;
        char time[8];
        std::snprintf(time, sizeof(time), "%02lu:%02lu", static_cast<unsigned long>(seconds / 60UL),
                      static_cast<unsigned long>(seconds % 60UL));
        const ui::themes::ColorRole phaseRole = focusPhase || complete ? ui::themes::ColorRole::Accent
                                                                       : ui::themes::ColorRole::BreakAccent;
        constexpr int16_t railWidth = 40;
        constexpr int16_t gap = 4;
        const ui::Rect hourglass{static_cast<int16_t>(area.x + railWidth + gap), area.y,
                                 static_cast<int16_t>(area.w - (railWidth + gap) * 2), area.h};
        ui.steps({area.x, area.y, railWidth, area.h}, session_.round(), session_.rounds(), phaseRole);
        ui.hourglass(hourglass, session_.progressPermille(nowMs), paused, complete, phaseRole, reversed, time);
        return ui.button({static_cast<int16_t>(area.x + area.w - railWidth), area.y, railWidth, area.h}, ">>");
    }

    void FocusScreen::edit(size_t index, bool creating, Screen& screen) {
        editIndex_ = index;
        creating_ = creating;
        draft_ = creating ? focus::defaultTimer() : timers_.items[index];
        if (creating)
            draft_.name.clear();
        deleteConfirm_ = false;
        keyboard_ = {};
        screen = Screen::FocusEditor;
    }

    bool FocusScreen::persist(const focus::Timers& timers) {
        return writable_ && filesystem_ != nullptr && focus::save(*filesystem_, timers);
    }

} // namespace screens
