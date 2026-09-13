#pragma once

#include <cstdint>

namespace screens {

    enum class Screen : uint8_t {
        Read,
        Library,
        Chapters,
        Settings,
        ReadingSettings,
        InterfaceSettings,
        PacingSettings,
        ReaderAppearance,
        BookFonts,
        NetworkSettings,
        WifiScan,
        WifiConnect,
        NetworkEdit,
        Device,
        StorageEncryption,
        Sync,
        Ota,
        FocusTimers,
        FocusEditor,
        FocusNameEdit,
        FocusSession,
        Reader,
        Usb,
        Status,
        Standby,
    };

    enum class Action : uint8_t {
        None,
        OpenBook,
        Resume,
        PowerOff,
        CompanionSync,
        RssRefresh,
        UsbTransfer,
        StorageStatus,
        EnableStorageEncryption,
        OtaCheck,
        OtaInstall,
    };

} // namespace screens
