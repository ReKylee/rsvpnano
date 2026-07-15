#include "board/BoardDisplay.h"

#include "board/BacklightBrightness.h"

#include <Arduino.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>

#include "display/RowPrefixCanvas.h"
#include "drivers/gpio/tca9554/Tca9554.h"
#include "platforms/waveshare_lcd_349/WaveshareLcd349.h"

namespace {

    constexpr int32_t kPanelBusHz = 40000000;

    Arduino_ESP32QSPI gBus(WaveshareLcd349::DisplayWiring::kCsPin, WaveshareLcd349::DisplayWiring::kSclkPin,
                           WaveshareLcd349::DisplayWiring::kData0Pin, WaveshareLcd349::DisplayWiring::kData1Pin,
                           WaveshareLcd349::DisplayWiring::kData2Pin, WaveshareLcd349::DisplayWiring::kData3Pin, false);

    Arduino_AXS15231B gPanel(&gBus, WaveshareLcd349::DisplayWiring::kResetPin, 0, false,
                             WaveshareLcd349::DisplayWiring::kPanelWidth, WaveshareLcd349::DisplayWiring::kPanelHeight,
                             0, 0, 0, 0);

    RowPrefixCanvas gCanvas(WaveshareLcd349::DisplayWiring::kPanelWidth, WaveshareLcd349::DisplayWiring::kPanelHeight,
                            &gPanel, 0, 0, 1);

    // This panel's PWM curve has a large dead zone at low duty values.
    // Keep the user-facing brightness scale at 1-100%, but map it onto
    // the usable hardware duty range so 5% is dim rather than off.
    constexpr uint8_t kMinimumBacklightDuty = 102;

    uint8_t gBacklightDuty = Board::Backlight::dutyFromPercent(100, kMinimumBacklightDuty);
    bool gBacklight = true;

    void writeBacklight() {
        if constexpr (WaveshareLcd349::DisplayWiring::kBacklightPin < 0) {
            return;
        }

        analogWriteResolution(WaveshareLcd349::DisplayWiring::kBacklightPin, 8);
        analogWriteFrequency(WaveshareLcd349::DisplayWiring::kBacklightPin, 25000);

        if (!gBacklight) {
            analogWrite(WaveshareLcd349::DisplayWiring::kBacklightPin, 255);
            return;
        }

        // Rev1 backlight is inverted: 0 = full on, 255 = off.
        analogWrite(WaveshareLcd349::DisplayWiring::kBacklightPin, 255 - gBacklightDuty);
    }

    void enableBacklightPower() {
        BoardDrivers::Tca9554::configureOutputPin(Wire1, WaveshareLcd349::Tca9554Wiring::kAddress,
                                                  WaveshareLcd349::Tca9554Wiring::kBacklightEnablePin, true,
                                                  WaveshareLcd349::Tca9554Wiring::kReleaseBusBeforeRead);
    }

    void logDisplayMemory() {
        Serial.printf("[display] host=%d mode=%d\n", static_cast<int>(ESP32QSPI_SPI_HOST),
                      static_cast<int>(ESP32QSPI_SPI_MODE));

        Serial.printf("[display] psramFound=%s size=%u free=%u\n", psramFound() ? "yes" : "no", ESP.getPsramSize(),
                      ESP.getFreePsram());

        Serial.printf("[display] heap internal=%u spiram=%u\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                      heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }

    void logCanvasMemory() {
        uint16_t* framebuffer = gCanvas.getFramebuffer();

        Serial.printf("[display] canvas framebuffer=%p external=%s\n", framebuffer,
                      esp_ptr_external_ram(framebuffer) ? "yes" : "no");

        Serial.printf("[display] after canvas: heap internal=%u spiram=%u\n",
                      heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }

} // namespace

namespace Board::Display {

    bool begin() {
        enableBacklightPower();

        pinMode(WaveshareLcd349::DisplayWiring::kBacklightPin, OUTPUT);

        logDisplayMemory();

        const bool ok = gCanvas.begin(kPanelBusHz);

        Serial.printf("[display] gCanvas.begin: %s\n", ok ? "ok" : "failed");
        Serial.printf("[display] canvas size=%dx%d\n", gCanvas.width(), gCanvas.height());

        if (!ok) {
            setBacklight(false);
            return false;
        }

        logCanvasMemory();

        writeBacklight();

        gCanvas.fillScreen(0x0000);
        gCanvas.flush();

        return true;
    }

    Arduino_GFX& gfx() {
        return gCanvas;
    }

    void holdBacklightOffForDeepSleep() {
        if constexpr (WaveshareLcd349::DisplayWiring::kBacklightPin < 0) {
            return;
        }

        analogWrite(WaveshareLcd349::DisplayWiring::kBacklightPin, 255);
        pinMode(WaveshareLcd349::DisplayWiring::kBacklightPin, OUTPUT);
        digitalWrite(WaveshareLcd349::DisplayWiring::kBacklightPin, HIGH);

        gpio_set_direction(WaveshareLcd349::DisplayWiring::kBacklightGpio, GPIO_MODE_OUTPUT);
        gpio_set_level(WaveshareLcd349::DisplayWiring::kBacklightGpio, 1);
        gpio_hold_en(WaveshareLcd349::DisplayWiring::kBacklightGpio);
        gpio_deep_sleep_hold_en();
    }

    ui::Orientation defaultUiOrientation() {
        return WaveshareLcd349::DisplayWiring::kDefaultUiOrientation;
    }

    ui::Orientation rotatedUiOrientation() {
        return ui::opposite(WaveshareLcd349::DisplayWiring::kDefaultUiOrientation);
    }

    uint16_t nativeWidth() {
        return WaveshareLcd349::DisplayWiring::kPanelWidth;
    }

    uint16_t nativeHeight() {
        return WaveshareLcd349::DisplayWiring::kPanelHeight;
    }

    size_t txChunkBytes() {
        return WaveshareLcd349::DisplayWiring::kTxChunkBytes;
    }

    void setBacklight(bool on) {
        gBacklight = on;
        writeBacklight();
    }

    void setBrightness(uint8_t percent) {
        gBacklightDuty = Board::Backlight::dutyFromPercent(percent, kMinimumBacklightDuty);
        writeBacklight();
    }

    void sleep() {
        gCanvas.fillScreen(0x0000);
        gCanvas.flush();

        setBacklight(false);
        gPanel.displayOff();
    }

    void wake() {
        gPanel.displayOn();
        setBacklight(true);

        // Redraw whatever is currently in the canvas framebuffer.
        gCanvas.flush(true);
    }

    bool pushColors(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint16_t* data) {
        gCanvas.draw16bitRGBBitmap(x, y, const_cast<uint16_t*>(data), width, height);

        // Important: do NOT flush here. Flush once at the end of the frame.
        return true;
    }

} // namespace Board::Display
