#include "app/App.h"
#include "app/screens/standby/StandbyTiming.h"
#include "app/screens/status/StatusScreen.h"

#include <esp_log.h>
#include <esp_system.h>
#include "board/BoardConfig.h"
#include "board/BoardInput.h"
#include "board/BoardPower.h"
#include "board/BoardSystem.h"
#include "library/ReadingProgress.h"

namespace {
    void powerOffBoard() {
        if (!Board::Power::powerOff())
            ESP_LOGI("app", "hardware power off unavailable; entering light sleep");
        delay(1200);
        Board::System::lightSleep(0);
        esp_restart();
    }
} // namespace

void App::enterStandby(uint32_t nowMs) {
    if (companionApi_.active()) {
        companionApi_.end();
    } else {
        networkScreen_.closeWifi();
    }
    ReadingProgress::save(readerScreen_.session, prefs_, true, nowMs);
    ReadingLoop::pause(readerScreen_.session);
    if (settingsStore_.settings().interface.screensaver == standby::Kind::screenOff) {
        screen_ = screens::Screen::Standby;
        lightSleepFromStandby();
        return;
    }
    Board::Display::wake();
    immediateUi_.invalidate();
    const int bookIndex = storage_.findBook(readerScreen_.session.sourcePath());
    standbyScreen_.begin(immediateUi_, nowMs, bookIndex < 0 ? 0 : static_cast<size_t>(bookIndex),
                         readerScreen_.session.state.wordIndex, settingsStore_.settings().interface.screensaver);
    standbyEnteredMs_ = nowMs;
    screen_ = screens::Screen::Standby;
    renderScreen(nowMs);
}

void App::exitStandby(uint32_t nowMs) {
    standbyScreen_.reset();
    Board::Display::wake();
    lastActivityMs_ = nowMs;
    screen_ = screens::Screen::Reader;
    renderScreen(nowMs);
}

void App::lightSleepFromStandby() {
    ESP_LOGI("app", "screen-off standby; entering light sleep");
    ReadingProgress::mirror(readerScreen_.session, readerScreen_.store);
    standbyScreen_.reset();
    if (usbTransfer_.active())
        usbTransfer_.end();
    settingsStore_.flush();
    Board::Display::sleep();
    Input::cancel();

    bool wokeByTouch = false;
    const uint32_t sleepStartedAtMs = millis();
    while (true) {
        const uint32_t elapsedMs = millis() - sleepStartedAtMs;
        if (elapsedMs >= screens::kStandbyPowerOffMs) {
            ESP_LOGI("app", "screen-off standby expired; powering off");
            powerOff(millis());
            return;
        }

        switch (Board::System::lightSleep(screens::kStandbyPowerOffMs - elapsedMs)) {
        case EspLightSleep::WakeReason::timer:
            ESP_LOGI("app", "screen-off standby expired; powering off");
            powerOff(millis());
            return;
        case EspLightSleep::WakeReason::input:
            if constexpr (Board::Config::HAS_LIGHT_SLEEP_TOUCH_IRQ) {
                ui::TouchContact contact = {};
                wokeByTouch = Board::Input::touchReady() && Board::Input::readTouch(contact) && contact.touched;
                const ::Input::PressActions controls = Board::Input::currentActions();
                const bool powerPressed = ::Input::hasAction(controls.longPress, ::Input::ActionPowerOff);
                if (!wokeByTouch && !powerPressed)
                    continue;
            }
            break;
        case EspLightSleep::WakeReason::error:
            break;
        }
        break;
    }

    const uint32_t wokeAtMs = millis();
    exitStandby(wokeAtMs);

    if (wokeByTouch) {
        const uint32_t releaseWaitStartedMs = millis();
        ui::TouchContact contact = {.touched = true};
        while (contact.touched && millis() - releaseWaitStartedMs < 1000) {
            delay(10);
            if (!Board::Input::readTouch(contact))
                break;
        }
    }
    // The sampler reinitializes touch on resume; finish the wake-contact reads first.
    Input::resume();
}

void App::powerOff(uint32_t nowMs) {
    if (companionApi_.active())
        companionApi_.end();
    if (screen_ == screens::Screen::FocusSession)
        focusScreen_.close();
    ReadingProgress::save(readerScreen_.session, prefs_, true, nowMs);
    ReadingProgress::mirror(readerScreen_.session, readerScreen_.store);
    ReadingLoop::pause(readerScreen_.session);
    screens::status(immediateUi_, immediateUi_.text(UiText::Off), immediateUi_.text(UiText::ReleasePower));
    delay(250);
    settingsStore_.flush();
    Board::Display::sleep();
    readerScreen_.store.close();
    storage_.end();
    Input::end();
    powerOffBoard();
}
