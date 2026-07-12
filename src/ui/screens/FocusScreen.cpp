#include "ui/screens/ScreenCommon.h"

#include <algorithm>
#include <cstdio>
#include <utility>

#include "timer/FocusTimerStorage.h"

namespace screens {
    namespace {

        constexpr size_t kTimersPerPage = 4;

    } // namespace

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
        const size_t pageCount = (timers_.count + kTimersPerPage - 1) / kTimersPerPage;
        page_ = std::min(page_, pageCount - 1);
        constexpr int16_t gap = 6;
        const int16_t cellWidth = static_cast<int16_t>((content.w - gap) / 2);
        const size_t first = page_ * kTimersPerPage;
        const size_t end = std::min(timers_.count, first + kTimersPerPage);
        for (size_t index = first; index < end; ++index) {
            const size_t local = index - first;
            const int16_t x = static_cast<int16_t>(content.x + (local % 2) * (cellWidth + gap));
            const int16_t y = static_cast<int16_t>(content.y + (local / 2) * 58);
            const ui::Rect cell{x, y, cellWidth, 52};
            const int16_t editWidth = 38;
            const focus::Timer& timer = timers_.items[index];
            char focusTime[8];
            char details[16];
            std::snprintf(focusTime, sizeof(focusTime), "%um", static_cast<unsigned int>(timer.focusMinutes));
            std::snprintf(details, sizeof(details), "%um · %ux", static_cast<unsigned int>(timer.breakMinutes),
                          static_cast<unsigned int>(timer.rounds));
            if (ui.button({cell.x, cell.y, static_cast<int16_t>(cell.w - editWidth - 4), cell.h}, timer.name,
                          orientation_.available(), ui::Icon::None, 1, focusTime, details)) {
                activeIndex_ = index;
                session_.begin(timer);
                screen = Screen::FocusSession;
            }
            if (ui.button({static_cast<int16_t>(cell.x + cell.w - editWidth), cell.y, editWidth, cell.h}, "", writable_,
                          ui::Icon::Edit)) {
                edit(index, false, screen);
            }
        }

        const int16_t controlsY = static_cast<int16_t>(content.y + content.h - 38);
        const int16_t controlWidth = static_cast<int16_t>((content.w - gap * 2) / 3);
        ui::Row controls{{content.x, controlsY, content.w, 38}, gap};
        if (ui.button(controls.next(controlWidth), ui.text(UiText::Previous), page_ > 0))
            --page_;
        if (ui.button(controls.next(controlWidth), ui.text(UiText::Add), writable_ && timers_.count < focus::kMaxTimers)) {
            edit(timers_.count, true, screen);
            draft_.name = ui.text(UiText::NewTimer);
        }
        if (ui.button(controls.next(controlWidth), ui.text(UiText::Next), page_ + 1 < pageCount))
            ++page_;

        if (!orientation_.available())
            ui.label({content.x, static_cast<int16_t>(controlsY - 8), content.w, 8}, ui.text(UiText::ImuUnavailable),
                     1, ui::themes::ColorRole::Muted, ui::TextAlign::Center);
    }

    void FocusScreen::drawEditor(ui::Context& ui, Screen& screen) {
        const ui::Rect content = detail::content(ui);
        if (ui.button({content.x, content.y, 64, 24}, ui.text(UiText::Back))) {
            deleteConfirm_ = false;
            screen = Screen::FocusTimers;
        }
        ui.label({static_cast<int16_t>(content.x + 74), content.y, static_cast<int16_t>(content.w - 74), 24},
                 ui.text(UiText::FocusTimer), 2);

        if (ui.setting({content.x, static_cast<int16_t>(content.y + 30), content.w, 34}, ui.text(UiText::TimerName),
                       draft_.name, ui::SettingLayout::Inline)) {
            keyboard_ = {};
            screen = Screen::FocusNameEdit;
        }

        constexpr int16_t gap = 6;
        const int16_t sliderWidth = static_cast<int16_t>((content.w - gap * 2) / 3);
        ui::Row sliders{{content.x, static_cast<int16_t>(content.y + 70), content.w, 50}, gap};
        if (const auto value = ui.slider(sliders.next(sliderWidth), ui.text(UiText::FocusMinutes),
                                         draft_.focusMinutes, 1, 180, 1, " min"); value.changed)
            draft_.focusMinutes = static_cast<uint16_t>(value.value);
        if (const auto value = ui.slider(sliders.next(sliderWidth), ui.text(UiText::BreakMinutes),
                                         draft_.breakMinutes, 1, 60, 1, " min"); value.changed)
            draft_.breakMinutes = static_cast<uint16_t>(value.value);
        if (const auto value = ui.slider(sliders.next(sliderWidth), ui.text(UiText::Rounds), draft_.rounds, 1, 12);
            value.changed)
            draft_.rounds = static_cast<uint8_t>(value.value);

        const ui::Rect actions{content.x, static_cast<int16_t>(content.y + 126), content.w, 30};
        const int16_t actionWidth = static_cast<int16_t>((actions.w - gap) / 2);
        ui::Row row{actions, gap};
        if (!deleteConfirm_) {
            if (ui.button(row.next(actionWidth), ui.text(UiText::Save), writable_ && focus::valid(draft_))) {
                focus::Timers updated = timers_;
                updated.items[editIndex_] = draft_;
                if (creating_)
                    ++updated.count;
                if (persist(updated)) {
                    timers_ = std::move(updated);
                    page_ = editIndex_ / kTimersPerPage;
                    screen = Screen::FocusTimers;
                }
            }
            if (ui.button(row.next(actionWidth), ui.text(UiText::Delete), writable_ && !creating_ && timers_.count > 1))
                deleteConfirm_ = true;
        } else {
            if (ui.button(row.next(actionWidth), ui.text(UiText::Back)))
                deleteConfirm_ = false;
            if (ui.button(row.next(actionWidth), ui.text(UiText::ConfirmDelete))) {
                focus::Timers updated = timers_;
                for (size_t index = editIndex_ + 1; index < updated.count; ++index)
                    updated.items[index - 1] = std::move(updated.items[index]);
                --updated.count;
                if (persist(updated)) {
                    timers_ = std::move(updated);
                    page_ = std::min(page_, (timers_.count - 1) / kTimersPerPage);
                    deleteConfirm_ = false;
                    screen = Screen::FocusTimers;
                }
            }
        }
    }

    void FocusScreen::drawNameEditor(ui::Context& ui, Screen& screen) {
        const ui::Rect content = detail::content(ui);
        ui.label({content.x, content.y, content.w, 22}, ui.text(UiText::TimerName), 2,
                 ui::themes::ColorRole::Foreground, ui::TextAlign::Center);
        const ui::KeyboardAction action =
            ui.keyboard({content.x, static_cast<int16_t>(content.y + 24), content.w,
                         static_cast<int16_t>(content.h - 24)}, draft_.name, 32, keyboard_);
        if (action == ui::KeyboardAction::Cancel || action == ui::KeyboardAction::Submit)
            screen = Screen::FocusEditor;
    }

    bool FocusScreen::drawSession(ui::Context& ui, uint32_t nowMs) {
        const ui::Rect area = detail::content(ui);
        const focus::Timer& timer = timers_.items[activeIndex_];
        const focus::Phase phase = session_.phase();
        const bool paused = phase == focus::Phase::PausedFocus || phase == focus::Phase::PausedBreak;
        const bool focusPhase = phase == focus::Phase::WaitingFocus || phase == focus::Phase::Focus
                             || phase == focus::Phase::PausedFocus;
        const bool complete = phase == focus::Phase::Complete;
        const bool hasInstruction = phase == focus::Phase::WaitingFocus || phase == focus::Phase::WaitingBreak;
        const UiText instruction = phase == focus::Phase::WaitingFocus && session_.round() == 1
                                     ? UiText::FlipToStart
                                 : phase == focus::Phase::WaitingFocus ? UiText::FlipForFocus
                                                                       : UiText::FlipForBreak;

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
        constexpr int16_t topHeight = 28;
        constexpr int16_t exitWidth = 54;
        constexpr int16_t timeWidth = 120;
        const int16_t timeX = static_cast<int16_t>(area.x + (area.w - timeWidth) / 2);
        ui.label({area.x, area.y, static_cast<int16_t>(timeX - area.x - 4), topHeight},
                 hasInstruction ? ui.text(instruction) : std::string_view{}, 1, ui::themes::ColorRole::Muted,
                 ui::TextAlign::Left, 2);
        ui.label({timeX, area.y, timeWidth, topHeight}, time, 3, phaseRole, ui::TextAlign::Center);
        const bool exit = ui.button({static_cast<int16_t>(area.x + area.w - exitWidth), area.y, exitWidth, topHeight},
                                    ui.text(UiText::Exit));

        const ui::Rect hourglass{area.x, static_cast<int16_t>(area.y + topHeight + 2), area.w,
                                 static_cast<int16_t>(area.h - topHeight - 18)};
        ui.hourglass(hourglass, session_.progressPercent(nowMs), paused, complete, phaseRole);
        ui.steps({area.x, static_cast<int16_t>(area.y + area.h - 14), area.w, 14}, session_.round(), session_.rounds(),
                 phaseRole);
        return exit;
    }

    void FocusScreen::edit(size_t index, bool creating, Screen& screen) {
        editIndex_ = index;
        creating_ = creating;
        draft_ = creating ? focus::defaultTimer() : timers_.items[index];
        deleteConfirm_ = false;
        keyboard_ = {};
        screen = Screen::FocusEditor;
    }

    bool FocusScreen::persist(const focus::Timers& timers) {
        return writable_ && filesystem_ != nullptr && focus::save(*filesystem_, timers);
    }

} // namespace screens
