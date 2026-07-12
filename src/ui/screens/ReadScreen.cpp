#include "ui/screens/ScreenCommon.h"

namespace screens {

    Action read(ui::Context& ui, const ReadModel& model, Screen& screen) {
        if (const Action action = detail::navigation(ui, Screen::Read, screen); action != Action::None) {
            return action;
        }

        const ui::Rect area = detail::tabContent(ui);
        const bool wide = ui.width() >= 620 && ui.height() >= 150 && ui.height() <= 240;
        ui::Column column{area, static_cast<int16_t>(wide ? 18 : 10)};
        const ui::Rect resume = column.next(64);
        const std::string_view author = model.author.empty() ? ui.text(UiText::Unknown) : model.author;
        char progress[4];
        size_t progressLength = 0;
        if (model.progress >= 100) {
            progress[progressLength++] = '1';
            progress[progressLength++] = '0';
            progress[progressLength++] = '0';
        } else {
            if (model.progress >= 10)
                progress[progressLength++] = static_cast<char>('0' + model.progress / 10);
            progress[progressLength++] = static_cast<char>('0' + model.progress % 10);
        }
        progress[progressLength++] = '%';
        const std::string_view progressText{progress, progressLength};
        if (ui.button(resume, model.title, true, ui::Icon::Bookmark, 2, author, progressText)) {
            return Action::Resume;
        }

        ui::Row actions{column.next(64), 14};
        const int16_t buttonWidth = static_cast<int16_t>((actions.bounds.w - actions.gap) / 2);
        if (ui.button(actions.next(buttonWidth), ui.text(UiText::Chapters))) {
            screen = Screen::Chapters;
        }
        if (ui.button(actions.next(buttonWidth), ui.text(UiText::Library))) {
            screen = Screen::Library;
        }
        return Action::None;
    }

} // namespace screens
