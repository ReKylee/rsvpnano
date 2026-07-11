#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include <span>
#include <string>

#include "book/BookMetadata.h"
#include "display/InterfaceSettings.h"
#include "display/ThemeStore.h"
#include "fonts/FontCatalog.h"
#include "reader/ReaderSettings.h"
#include "reader/ReadingLoop.h"
#include "standby/ScreensaverTypes.h"
#include "timer/FocusTimer.h"
#include "ui/Ui.h"

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
        Device,
        Sync,
        Ota,
        FocusGenres,
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
        OtaCheck,
        OtaInstall,
        ApplyReaderOrientation,
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

        void begin(ui::Context& ui, Preferences& preferences, size_t standbyDurationCount,
                   void (*setBrightness)(uint8_t));
        void draw(ui::Context& ui, Preferences& preferences, std::span<const uint32_t> standbyDurations,
                  void (*setBrightness)(uint8_t), Screen& screen);
    };
    void pacingSettings(ui::Context& ui, ReadingLoop& reader, Preferences& preferences, Screen& screen);
    void typographySettings(ui::Context& ui, ReaderSettings& settings, FontCatalog& fonts, Preferences& preferences,
                            Screen& screen);
    Action readerSettings(ui::Context& ui, ReaderSettings& settings, Preferences& preferences, Screen& screen);
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
    };
    Action device(ui::Context& ui, bool storageReady, size_t bookCount, Screen& screen);
    Action sync(ui::Context& ui, Screen& screen);
    Action ota(ui::Context& ui, Screen& screen);
    class FocusScreen {
    public:
        FocusTimer timer;

        bool genres(ui::Context& ui, uint32_t nowMs, Screen& screen);
        void session(ui::Context& ui, uint32_t nowMs);
    };
    void status(ui::Context& ui, std::string_view title, std::string_view line1 = {}, std::string_view line2 = {},
                int progress = -1);

} // namespace screens
