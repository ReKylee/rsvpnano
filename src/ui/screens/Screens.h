#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include <span>
#include <array>
#include <string>

#include "book/BookMetadata.h"
#include "display/InterfaceSettings.h"
#include "display/ThemeStore.h"
#include "fonts/FontCatalog.h"
#include "reader/ReaderSettings.h"
#include "reader/ReadingLoop.h"
#include "settings/NvsSecurity.h"
#include "standby/ScreensaverTypes.h"
#include "timer/FocusOrientation.h"
#include "timer/FocusSession.h"
#include "timer/FocusTimers.h"
#include "ui/Ui.h"

namespace fs {
    class FS;
}

namespace screens {

    enum class Screen : uint8_t {
        Read,
        Library,
        Chapters,
        Settings,
        ReadingSettings,
        InterfaceSettings,
        PacingSettings,
        TypographySettings,
        ReaderSettings,
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

    struct ReadModel {
        std::string title;
        std::string author;
        uint8_t progress = 0;
    };

    Action read(ui::Context& ui, const ReadModel& model, Screen& screen);
    Action settings(ui::Context& ui, Screen& screen);
    void readingSettings(ui::Context& ui, ReadingLoop& reader, ReaderSettings& settings, Preferences& preferences,
                         Screen& screen);
    class InterfaceScreen {
    public:
        InterfaceSettings config;
        ThemeStore themes;

        void begin(ui::Context& ui, Preferences& preferences, void (*setBrightness)(uint8_t));
        void draw(ui::Context& ui, Preferences& preferences, std::span<const uint32_t> standbyDurations,
                  void (*setBrightness)(uint8_t), Screen& screen);
    };
    void pacingSettings(ui::Context& ui, ReadingLoop& reader, Preferences& preferences, Screen& screen);
    void typographySettings(ui::Context& ui, ReaderSettings& settings, FontCatalog& fonts, Preferences& preferences,
                            Screen& screen);
    void readerSettings(ui::Context& ui, ReaderSettings& settings, Preferences& preferences, Screen& screen);
    class NetworkScreen {
    public:
        std::string ssid;
        std::string owner;
        std::string tag;
        bool automatic = false;
        bool autoCheckPending = false;
        bool ssidStored = false;

        void begin(Preferences& preferences);
        void draw(ui::Context& ui, Preferences& preferences, Screen& screen);
        void openWifiScan();
        void closeWifi();
        void drawWifiScan(ui::Context& ui, Preferences& preferences, Screen& screen);
        bool drawWifiConnect(ui::Context& ui, Preferences& preferences, Screen& screen);
        void drawEdit(ui::Context& ui, Preferences& preferences, Screen& screen);

    private:
        struct WifiNetwork {
            std::string ssid;
            int32_t rssi = 0;
            bool secured = false;
        };

        enum class WifiScanState : uint8_t {
            Idle,
            Scanning,
            Complete,
            Failed,
        };

        enum class EditField : uint8_t {
            Owner,
            Tag,
        };

        std::array<WifiNetwork, 8> networks_;
        size_t networkCount_ = 0;
        std::string password_;
        std::string editValue_;
        ui::KeyboardState keyboard_;
        EditField editField_ = EditField::Owner;
        WifiScanState scanState_ = WifiScanState::Idle;
        bool connectionFailed_ = false;
    };
    Action device(ui::Context& ui, bool storageReady, size_t bookCount,
                  settings::NvsEncryptionState encryptionState, Screen& screen);
    Action storageEncryption(ui::Context& ui, settings::NvsEncryptionState encryptionState, Screen& screen);
    Action sync(ui::Context& ui, Screen& screen);
    Action ota(ui::Context& ui, Screen& screen);
    class FocusScreen {
    public:
        void begin(fs::FS* filesystem);
        bool update(uint32_t nowMs);
        void draw(ui::Context& ui, uint32_t nowMs, Screen& screen);
        void close();

    private:
        void drawTimers(ui::Context& ui, Screen& screen);
        void drawEditor(ui::Context& ui, Screen& screen);
        void drawNameEditor(ui::Context& ui, Screen& screen);
        bool drawSession(ui::Context& ui, uint32_t nowMs);
        void edit(size_t index, bool creating, Screen& screen);
        bool persist(const focus::Timers& timers);

        fs::FS* filesystem_ = nullptr;
        focus::Timers timers_;
        focus::Timer draft_;
        focus::Session session_;
        focus::OrientationReader orientation_;
        ui::KeyboardState keyboard_;
        size_t editIndex_ = 0;
        size_t activeIndex_ = 0;
        bool creating_ = false;
        bool deleteConfirm_ = false;
        bool writable_ = false;
    };
    void status(ui::Context& ui, std::string_view title, std::string_view line1 = {}, std::string_view line2 = {},
                int progress = -1);

} // namespace screens
