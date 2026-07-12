#include "ui/screens/ScreenCommon.h"

namespace screens {
    namespace {
        bool encryptionAcknowledged = false;
    }

    Action device(ui::Context& ui, bool storageReady, size_t bookCount,
                  settings::NvsEncryptionState encryptionState, Screen& screen) {
        if (const Action action = detail::navigation(ui, Screen::Device, screen); action != Action::None)
            return action;
        ui::Grid grid{detail::tabContent(ui), static_cast<uint8_t>(ui.width() >= 400 ? 3 : ui.width() >= 240 ? 2 : 1),
                      54, 8};
        std::string storage{ui.text(storageReady ? UiText::StorageReady : UiText::StorageUnavailable)};
        storage += " · ";
        storage += std::to_string(bookCount);
        storage += " ";
        storage += ui.text(UiText::Items);

        std::string encryption{ui.text(UiText::StorageEncryption)};
        encryption += " · ";
        encryption += ui.text(encryptionState == settings::NvsEncryptionState::Enabled ? UiText::On
                            : encryptionState == settings::NvsEncryptionState::Available && storageReady
                                ? UiText::Off
                                : UiText::Unavailable);

        if (ui.button(grid.next(), storage))
            return Action::StorageStatus;
        if (ui.button(grid.next(), ui.text(UiText::UsbTransfer)))
            return Action::UsbTransfer;
        const ui::Rect encryptionButton = grid.next();
        if (ui.button(encryptionButton, encryption) && storageReady
            && encryptionState == settings::NvsEncryptionState::Available) {
            encryptionAcknowledged = false;
            screen = Screen::StorageEncryption;
        }
        const ui::Touch* touch = ui.touch();
        if (encryptionState == settings::NvsEncryptionState::Enabled && touch != nullptr
            && ui::hasTouch(*touch, ui::TouchHold) && ui::contains(encryptionButton, touch->x, touch->y)) {
            encryptionAcknowledged = false;
            screen = Screen::StorageEncryption;
        }
        if (ui.button(grid.next(), ui.text(UiText::CompanionSync))) {
            screen = Screen::Sync;
            return Action::CompanionSync;
        }
        if (ui.button(grid.next(), ui.text(UiText::RefreshRss)))
            return Action::RssRefresh;
        if (ui.button(grid.next(), ui.text(UiText::OtaUpdate)))
            screen = Screen::Ota;
        return Action::None;
    }

    Action storageEncryption(ui::Context& ui, settings::NvsEncryptionState encryptionState, Screen& screen) {
        if (const Action action = detail::navigation(ui, Screen::StorageEncryption, screen); action != Action::None) {
            encryptionAcknowledged = false;
            return action;
        }
        if (screen != Screen::StorageEncryption) {
            encryptionAcknowledged = false;
            return Action::None;
        }

        ui::Column content{detail::tabContent(ui), 2};
        ui.label(content.next(18), ui.text(UiText::StorageEncryption), 2, ui::themes::ColorRole::Foreground,
                 ui::TextAlign::Center);
        ui.label(content.next(24), ui.text(UiText::StorageEncryptionExplanation), 1,
                 ui::themes::ColorRole::Foreground,
                 ui::TextAlign::Center, 2);
        ui.label(content.next(24), ui.text(UiText::StorageEncryptionPermanent), 1, ui::themes::ColorRole::Muted,
                 ui::TextAlign::Center, 2);
        ui.label(content.next(24), ui.text(UiText::StorageEncryptionReset), 1, ui::themes::ColorRole::Muted,
                 ui::TextAlign::Center, 2);
        if (ui.toggle(content.next(24), ui.text(UiText::IUnderstand), encryptionAcknowledged))
            encryptionAcknowledged = !encryptionAcknowledged;

        const ui::Rect actions = content.next(28);
        const int16_t buttonWidth = static_cast<int16_t>((actions.w - 8) / 2);
        ui::Row row{actions, 8};
        const ui::Rect back = encryptionAcknowledged ? row.next(buttonWidth) : actions;
        if (ui.button(back, ui.text(UiText::Back))) {
            encryptionAcknowledged = false;
            screen = Screen::Device;
        }
        if (encryptionAcknowledged && ui.button(row.next(buttonWidth), ui.text(UiText::EnableProtection),
                      encryptionState == settings::NvsEncryptionState::Available)) {
            encryptionAcknowledged = false;
            return Action::EnableStorageEncryption;
        }
        return Action::None;
    }

} // namespace screens
