#include "app/App.h"

#include <esp_log.h>
#include <string>
#include "board/BoardStorage.h"
#include "library/ReadingProgress.h"

void App::enterUsbTransfer(uint32_t nowMs) {
#if RSVP_USB_TRANSFER_ENABLED
    if (serialCompanion_.active())
        return;
    ReadingProgress::save(readerScreen_.session, prefs_, true, nowMs);
    ReadingProgress::mirror(readerScreen_.session, readerScreen_.store);
    if (auto result = settingsStore_.flush(); !result) {
        showTransientStatus("USB", immediateUi_.text(UiText::CouldNotStart), result.error().message, 1200,
                            screens::Screen::Reader);
        return;
    }
    ReadingLoop::pause(readerScreen_.session);
    readerScreen_.store.close();
    storage_.end();
    if (!usbTransfer_.begin(true)) {
        const std::string failure = usbTransfer_.statusMessage();
        exitUsbTransfer();
        showTransientStatus("USB", immediateUi_.text(UiText::CouldNotStart), failure, 1200, screens::Screen::Reader);
        return;
    }
    screen_ = screens::Screen::Usb;
    renderScreen(nowMs);
#else
    showTransientStatus("USB", immediateUi_.text(UiText::Unavailable), {}, 1000, screens::Screen::Reader);
#endif
}

void App::exitUsbTransfer(screens::Screen destination) {
    usbTransfer_.end();
    const bool mounted = storage_.begin();
    fs::FS* filesystem = mounted ? &Board::Storage::filesystem() : nullptr;
    if (auto result = settingsStore_.begin(filesystem); !result)
        ESP_LOGW("settings", "USB transfer reload warning: %s", result.error().message.c_str());
    localeCatalog_ = mounted ? locales::scanInstalled(*filesystem, static_cast<size_t>(UiText::Count))
                             : locales::Catalog{};
    if (mounted) {
        readerScreen_.fonts.loadFromSd();
        focusScreen_.begin(*filesystem);
        readerScreen_.loadInitialBook(immediateUi_, storage_, prefs_, millis());
    } else {
        focusScreen_.begin();
    }
    applySettings();
    libraryScreen_.invalidate();
    screen_ = destination;
    renderScreen(millis());
}
