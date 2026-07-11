#include "ui/screens/ScreenCommon.h"

namespace screens {

    Action device(ui::Context& ui, bool storageReady, size_t bookCount, Screen& screen) {
        if (const Action action = detail::navigation(ui, Screen::Device, screen); action != Action::None)
            return action;
        ui::Grid grid{detail::tabContent(ui), static_cast<uint8_t>(ui.width() >= 400 ? 3 : 1), 54, 8};
        std::string storage{ui.text(storageReady ? UiText::StorageReady : UiText::StorageUnavailable)};
        storage += " · ";
        storage += std::to_string(bookCount);
        storage += " ";
        storage += ui.text(UiText::Items);
        if (ui.button(grid.next(), storage))
            return Action::StorageStatus;
        if (ui.button(grid.next(), ui.text(UiText::SyncImport)))
            screen = Screen::Sync;
        if (ui.button(grid.next(), ui.text(UiText::OtaUpdate)))
            screen = Screen::Ota;
        return Action::None;
    }

} // namespace screens
