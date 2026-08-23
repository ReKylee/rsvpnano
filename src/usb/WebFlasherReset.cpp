#include "usb/WebFlasherReset.h"

#include <Arduino.h>
#include <sdkconfig.h>

#if SOC_USB_OTG_SUPPORTED && CONFIG_TINYUSB_CDC_ENABLED && !ARDUINO_USB_MODE && ARDUINO_USB_CDC_ON_BOOT
#include <USBCDC.h>
#include <esp32-hal-tinyusb.h>

namespace {

    usb::WebFlasherResetSequence resetSequence;

    void onLineState(void*, esp_event_base_t, int32_t, void* eventData) {
        const auto& lineState = static_cast<arduino_usb_cdc_event_data_t*>(eventData)->line_state;
        if (resetSequence.update(lineState.dtr, lineState.rts))
            usb_persist_restart(RESTART_BOOTLOADER);
    }

} // namespace
#endif

void usb::enableWebFlasherReset() {
#if SOC_USB_OTG_SUPPORTED && CONFIG_TINYUSB_CDC_ENABLED && !ARDUINO_USB_MODE && ARDUINO_USB_CDC_ON_BOOT
    Serial.onEvent(ARDUINO_USB_CDC_LINE_STATE_EVENT, onLineState);
#endif
}
