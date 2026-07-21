#include "board/BoardDisplay.h"
#include <esp_log.h>

#include "board/BacklightBrightness.h"

#include <Arduino.h>
#include <Wire.h>
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
                            gPanel, 0, 0, 1);

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

        if (!gBacklight) {
            pinMode(WaveshareLcd349::DisplayWiring::kBacklightPin, OUTPUT);
            digitalWrite(WaveshareLcd349::DisplayWiring::kBacklightPin, HIGH);
            return;
        }

        analogWriteResolution(WaveshareLcd349::DisplayWiring::kBacklightPin, 8);
        analogWriteFrequency(WaveshareLcd349::DisplayWiring::kBacklightPin, 25000);

        // Rev1 backlight is inverted: 0 = full on, 255 = off.
        analogWrite(WaveshareLcd349::DisplayWiring::kBacklightPin, 255 - gBacklightDuty);
    }

    void setBacklightPower(bool enabled) {
        BoardDrivers::Tca9554::configureOutputPin(Wire1, WaveshareLcd349::Tca9554Wiring::kAddress,
                                                  WaveshareLcd349::Tca9554Wiring::kBacklightEnablePin, enabled,
                                                  WaveshareLcd349::Tca9554Wiring::kReleaseBusBeforeRead);
    }

    void logDisplayMemory() {
        ESP_LOGD("display", "host=%d mode=%d", static_cast<int>(ESP32QSPI_SPI_HOST),
                      static_cast<int>(ESP32QSPI_SPI_MODE));

        ESP_LOGD("display", "psramFound=%s size=%u free=%u", psramFound() ? "yes" : "no", ESP.getPsramSize(),
                      ESP.getFreePsram());

        ESP_LOGD("display", "heap internal=%u spiram=%u", heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                      heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }

    void logCanvasMemory() {
        uint16_t* framebuffer = gCanvas.getFramebuffer();

        ESP_LOGD("display", "canvas framebuffer=%p external=%s", framebuffer,
                      esp_ptr_external_ram(framebuffer) ? "yes" : "no");

        ESP_LOGD("display", "after canvas: heap internal=%u spiram=%u",
                      heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }

} // namespace

namespace Board::Display {

    bool begin() {
        setBacklightPower(true);

        pinMode(WaveshareLcd349::DisplayWiring::kBacklightPin, OUTPUT);

        if constexpr (WaveshareLcd349::DisplayWiring::kResetPin < 0) {
            BoardDrivers::Tca9554::configureOutputPin(Wire1, WaveshareLcd349::Tca9554Wiring::kAddress, 5, true);
        }

        logDisplayMemory();

        const bool ok = gCanvas.begin(kPanelBusHz);

        if (ok)
            ESP_LOGI("display", "canvas ready");
        else
            ESP_LOGE("display", "canvas initialization failed");
        ESP_LOGD("display", "canvas size=%dx%d", gCanvas.width(), gCanvas.height());

        if (!ok) {
            setBacklight(false);
            return false;
        }

        // Rev1 routes the unused panel TE output through the touch IRQ's shared TCA9554 interrupt.
        gBus.sendCommand(0x34); // TEOFF

        logCanvasMemory();

        gCanvas.fillScreen(0x0000);
        gCanvas.flush();
        writeBacklight();

        return true;
    }

    Arduino_GFX& gfx() {
        return gCanvas;
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
        setBacklight(false);
        setBacklightPower(false);
    }

    void wake() {
        // Redraw whatever is currently in the canvas framebuffer.
        gCanvas.flush(true);
        setBacklightPower(true);
        setBacklight(true);
    }

    bool pushColors(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint16_t* data) {
        gCanvas.draw16bitRGBBitmap(x, y, const_cast<uint16_t*>(data), width, height);

        // Important: do NOT flush here. Flush once at the end of the frame.
        return true;
    }

} // namespace Board::Display
