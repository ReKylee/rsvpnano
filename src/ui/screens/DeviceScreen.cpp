#include "ui/screens/ScreenCommon.h"

namespace screens {
    namespace {
        bool encryptionAcknowledged = false;
    }

    Action device(ui::Context& ui, bool storageReady, size_t bookCount,
                  settings::NvsEncryptionState encryptionState, Screen& screen) {
        if (const Action action = detail::navigation(ui, Screen::Device, screen); action != Action::None)
            return action;
        const ui::Rect content = detail::tabContent(ui);
        constexpr int16_t gap = 4;

        std::string items{std::to_string(bookCount)};
        items += " ";
        items += ui.text(UiText::Items);
        const std::string_view encryptionStatus =
            ui.text(encryptionState == settings::NvsEncryptionState::Enabled ? UiText::On
                        : encryptionState == settings::NvsEncryptionState::Available && storageReady
                            ? UiText::Off
                            : UiText::Unavailable);

        ui::Rect storageButton;
        ui::Rect encryptionButton;
        ui::Grid actions;
        if (content.w >= 280) {
            constexpr int16_t statusHeight = 60;
            const int16_t statusWidth = static_cast<int16_t>((content.w - gap) / 2);
            storageButton = {content.x, content.y, statusWidth, statusHeight};
            encryptionButton = {static_cast<int16_t>(content.x + statusWidth + gap), content.y,
                                static_cast<int16_t>(content.w - statusWidth - gap), statusHeight};
            const int16_t actionsY = static_cast<int16_t>(content.y + statusHeight + gap);
            const int16_t actionsHeight = static_cast<int16_t>(content.y + content.h - actionsY);
            actions = {{content.x, actionsY, content.w, actionsHeight}, 2,
                       static_cast<int16_t>((actionsHeight - gap) / 2), gap};
        } else {
            ui::Grid all{content, 1, static_cast<int16_t>((content.h - gap * 5) / 6), gap};
            storageButton = all.next();
            encryptionButton = all.next();
            actions = all;
        }

        if (ui.setting(storageButton, ui.text(storageReady ? UiText::StorageReady : UiText::StorageUnavailable),
                       items))
            return Action::StorageStatus;
        if (ui.setting(encryptionButton, ui.text(UiText::StorageEncryption), encryptionStatus)
            && storageReady
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
        if (ui.button(actions.next(), ui.text(UiText::UsbTransfer), true, ui::Icon::None, 2))
            return Action::UsbTransfer;
        if (ui.button(actions.next(), ui.text(UiText::CompanionSync), true, ui::Icon::None, 2)) {
            screen = Screen::Sync;
            return Action::CompanionSync;
        }
        if (ui.button(actions.next(), ui.text(UiText::RefreshRss), true, ui::Icon::None, 2))
            return Action::RssRefresh;
        if (ui.button(actions.next(), ui.text(UiText::OtaUpdate), true, ui::Icon::None, 2))
            screen = Screen::Ota;
        return Action::None;
    }

    Action storageEncryption(ui::Context& ui, settings::NvsEncryptionState encryptionState, Screen& screen) {
        if (screen != Screen::StorageEncryption) {
            encryptionAcknowledged = false;
            return Action::None;
        }

        const ui::Rect content = detail::content(ui);
        constexpr int16_t gap = 8;
        constexpr int16_t actionsWidth = 230;
        const ui::Rect explanation{content.x, content.y,
                                   static_cast<int16_t>(content.w - actionsWidth - gap), content.h};
        const ui::Rect actionArea{static_cast<int16_t>(explanation.x + explanation.w + gap), content.y,
                                  actionsWidth, content.h};

        ui::Column notes{explanation, 3};
        ui.label(notes.next(50), ui.text(UiText::StorageEncryptionExplanation), 2,
                 ui::themes::ColorRole::Foreground, ui::TextAlign::Left, 3);
        ui.label(notes.next(50), ui.text(UiText::StorageEncryptionPermanent), 2,
                 ui::themes::ColorRole::Accent, ui::TextAlign::Left, 3);
        ui.label(notes.next(50), ui.text(UiText::StorageEncryptionReset), 2,
                 ui::themes::ColorRole::Foreground, ui::TextAlign::Left, 3);

        ui::Column actions{actionArea, 1};
        ui.label(actions.next(32), ui.text(UiText::StorageEncryption), 2,
                 ui::themes::ColorRole::Foreground, ui::TextAlign::Left, 2);
        if (ui.toggle(actions.next(40), ui.text(UiText::IUnderstand), encryptionAcknowledged))
            encryptionAcknowledged = !encryptionAcknowledged;
        if (ui.button(actions.next(40), ui.text(UiText::EnableProtection),
                      encryptionAcknowledged && encryptionState == settings::NvsEncryptionState::Available)) {
            encryptionAcknowledged = false;
            return Action::EnableStorageEncryption;
        }
        if (ui.button(actions.next(40), ui.text(UiText::Back))) {
            encryptionAcknowledged = false;
            screen = Screen::Device;
        }
        return Action::None;
    }

} // namespace screens
