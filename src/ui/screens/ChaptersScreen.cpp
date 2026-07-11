#include "ui/screens/ScreenCommon.h"

namespace screens {

Action chapters(ui::Context& ui, std::span<const ChapterMarker> chapters, ReadingLoop& reader, Screen& screen) {
    ui::Column column{detail::content(ui), 4};
    if (ui.button(column.next(22), "Back")) {
        screen = Screen::Read;
        return Action::None;
    }
    const size_t visible = std::min<size_t>(chapters.size(), std::max<int16_t>(0, (column.bounds.h - 26) / 22));
    for (size_t index = 0; index < visible; ++index) {
        const String& title = chapters[index].title;
        if (ui.button(column.next(18), title.isEmpty() ? "Chapter" : title.c_str())) {
            reader.seekTo(chapters[index].wordIndex);
            return Action::Resume;
        }
    }
    if (chapters.empty() && ui.button(column.next(22), "Start")) {
        reader.seekTo(0);
        return Action::Resume;
    }
    return Action::None;
}

} // namespace screens
