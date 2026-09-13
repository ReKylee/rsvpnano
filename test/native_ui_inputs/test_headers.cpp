// Compile without platform stubs: menu data must not require device or screen owners.
#include "ui/screens/SettingsScreen.h"
#include "ui/screens/Screen.h"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace {
    constexpr std::array screenIds{
        screens::Screen::Read, screens::Screen::Library, screens::Screen::Chapters,
        screens::Screen::Settings, screens::Screen::ReadingSettings, screens::Screen::InterfaceSettings,
        screens::Screen::PacingSettings, screens::Screen::ReaderAppearance, screens::Screen::BookFonts,
        screens::Screen::NetworkSettings, screens::Screen::WifiScan, screens::Screen::WifiConnect,
        screens::Screen::NetworkEdit, screens::Screen::Device, screens::Screen::StorageEncryption,
        screens::Screen::Sync, screens::Screen::Ota, screens::Screen::FocusTimers,
        screens::Screen::FocusEditor, screens::Screen::FocusNameEdit, screens::Screen::FocusSession,
        screens::Screen::Reader, screens::Screen::Usb, screens::Screen::Status, screens::Screen::Standby,
    };
    constexpr std::array actionIds{
        screens::Action::None, screens::Action::OpenBook, screens::Action::Resume,
        screens::Action::PowerOff, screens::Action::CompanionSync, screens::Action::RssRefresh,
        screens::Action::UsbTransfer, screens::Action::StorageStatus, screens::Action::EnableStorageEncryption,
        screens::Action::OtaCheck, screens::Action::OtaInstall,
    };
    constexpr auto consecutive = [](auto values) {
        for (std::size_t index = 0; index < values.size(); ++index)
            if (std::to_underlying(values[index]) != index)
                return false;
        return true;
    };
    static_assert(consecutive(screenIds) && consecutive(actionIds));
    static_assert(std::same_as<std::underlying_type_t<screens::Screen>, uint8_t>);
    static_assert(std::same_as<std::underlying_type_t<screens::Action>, uint8_t>);
    static_assert(screens::settingsPage::entries.size() == 5);
    static_assert(screens::settingsPage::entries[2].destination == screens::Screen::ReaderAppearance);
    static_assert(screens::settingsPage::entries[2].span == ui::Grid::FullRow);
}

int main() {}
