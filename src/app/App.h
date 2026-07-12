#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include "board/BoardDisplay.h"
#include "input/Input.h"
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
    void renderScreen(uint32_t nowMs);
    void handleScreenAction(screens::Action action, uint32_t nowMs);
    void handleInput(const Input::Event& event, uint32_t nowMs);
    void handleTouch(uint32_t nowMs);
    void runRss();
    void enterUsbTransfer(uint32_t nowMs);
    void exitUsbTransfer();
    void runOtaCheck(bool install);
    void enterStandby(uint32_t nowMs);
    void exitStandby(uint32_t nowMs);
    void deepSleepFromStandby(uint32_t nowMs);
    void powerOff(uint32_t nowMs);
    static void renderStorageStatus(void* context, const char* title, const char* line1, const char* line2,
                                    int progressPercent);

    ui::Context immediateUi_{Board::Display::gfx(), &Board::Display::flush, &Board::Display::flushRegion};
    screens::ReaderScreen readerScreen_{Board::Display::gfx()};
    screens::LibraryScreen libraryScreen_;
    screens::ChaptersScreen chaptersScreen_;
    screens::InterfaceScreen interfaceScreen_;
    screens::NetworkScreen networkScreen_;
    StorageManager storage_;
    CompanionSyncManager sync_;
    UsbMassStorageManager usbTransfer_;
    screens::FocusScreen focusScreen_;
    screens::StandbyScreen standbyScreen_;
    Preferences prefs_;

    screens::Screen screen_ = screens::Screen::Status;
    uint32_t bootMs_ = 0;
    uint32_t lastActivityMs_ = 0;
};
