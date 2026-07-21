#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include "board/BoardDisplay.h"
#include "board/BoardPower.h"
#include "input/Input.h"
#include "settings/SettingsStore.h"
#include "storage/StorageManager.h"
#include "sync/CompanionSyncManager.h"
#include "ui/Ui.h"
#include "ui/screens/ChaptersScreen.h"
#include "ui/screens/LibraryScreen.h"
#include "ui/screens/ReaderScreen.h"
#include "ui/screens/Screens.h"
#include "ui/screens/StandbyScreen.h"
#include "usb/UsbMassStorageManager.h"

class App {
public:
    void begin();
    void update(uint32_t nowMs);

private:
    void migrateLegacyStorage();
    void renderScreen(uint32_t nowMs);
    void handleScreenAction(screens::Action action, uint32_t nowMs);
    void handleInput(const Input::Event& event, uint32_t nowMs);
    void handleTouch(uint32_t nowMs);
    void runRss();
    void reloadSettings();
    void enterUsbTransfer(uint32_t nowMs);
    void exitUsbTransfer(screens::Screen destination = screens::Screen::Reader);
    void runOtaCheck(bool install);
    void enterStandby(uint32_t nowMs);
    void exitStandby(uint32_t nowMs);
    void lightSleepFromStandby();
    void powerOff(uint32_t nowMs);
    static void renderStorageStatus(void* context, const char* title, const char* line1, const char* line2,
                                    int progressPercent);

    ui::Context immediateUi_{Board::Display::gfx()};
    settings::SettingsStore settingsStore_;
    Board::Power::BatteryState battery_;
    screens::ReaderScreen readerScreen_{Board::Display::gfx(), settingsStore_.settings().reading};
    screens::LibraryScreen libraryScreen_;
    screens::ChaptersScreen chaptersScreen_;
    screens::InterfaceScreen interfaceScreen_;
    screens::NetworkScreen networkScreen_;
    StorageManager storage_;
    Preferences prefs_;
    CompanionSyncManager sync_{settingsStore_};
    UsbMassStorageManager usbTransfer_;
    screens::FocusScreen focusScreen_;
    screens::StandbyScreen standbyScreen_;
    screens::Screen screen_ = screens::Screen::Status;
    uint32_t bootMs_ = 0;
    uint32_t lastActivityMs_ = 0;
    uint32_t standbyEnteredMs_ = 0;
};
