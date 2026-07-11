#include "ui/screens/ScreenCommon.h"

namespace screens {

    Action read(ui::Context& ui, const ReadModel& model, Screen& screen) {
        if (const Action action = detail::navigation(ui, Screen::Read, screen); action != Action::None) {
            return action;
        }

        const ui::Rect area = detail::content(ui);
        const bool wide = ui.width() >= 620 && ui.height() >= 150 && ui.height() <= 240;
        ui::Column column{area, static_cast<int16_t>(wide ? 20 : 10)};
        const ui::Rect resume = column.next(54);
        const ui::Rect resumeButton{resume.x, resume.y, resume.w, 44};
        if (ui.button(resumeButton, model.title.c_str(), ui::Icon::Bookmark)) {
            return Action::Resume;
        }
        ui.progress({static_cast<int16_t>(resume.x + 10), static_cast<int16_t>(resume.y + 49),
                     static_cast<int16_t>(resume.w - 20), 3},
                    model.progress);

        ui::Row actions{column.next(64), 14};
        const int16_t buttonWidth = static_cast<int16_t>((actions.bounds.w - actions.gap) / 2);
        if (ui.button(actions.next(buttonWidth), "Chapters")) {
            screen = Screen::Chapters;
        }
        if (ui.button(actions.next(buttonWidth), "Library")) {
            screen = Screen::Library;
        }
        return Action::None;
    }

} // namespace screens
