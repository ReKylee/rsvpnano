#pragma once
#include "ui/Ui.h"
#include "ui/Layouts.h"
#include "settings/SettingsModel.h"
namespace screens {
    enum class Screen : uint8_t {
        Read, Library, Chapters, Settings, ReadingSettings, InterfaceSettings, PacingSettings,
        ReaderAppearance, BookFonts, NetworkSettings, WifiScan, WifiConnect, NetworkEdit,
        Device, StorageEncryption, Sync, Ota, FocusTimers, FocusEditor, FocusNameEdit, FocusSession,
        Reader, Usb, Status, Standby,
    };
    enum class Action : uint8_t { None, PowerOff };
    Action settings(ui::Context&, Screen&);
    bool readingSettings(ui::Context&, settings::ReadingSettings&, Screen&);
}
