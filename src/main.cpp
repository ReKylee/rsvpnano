#include <Arduino.h>
#include <esp_log.h>
#include "app/App.h"
#include "board/Board.h"
#include "logging/Logger.h"
#include "settings/NvsSecurity.h"

App app;

void setup() {
    Serial.begin(115200);
    Logger::begin();
    delay(50);
    Board::System::begin();
    const uint32_t serialWaitStart = millis();
    while (!Serial && millis() - serialWaitStart < 2000) {
        delay(10);
    }
    Board::System::logStartupDiagnostics();
    if (!settings::initializeNvsEncryption()) {
        ESP_LOGE("main", "encrypted NVS initialization failed; restarting");
        delay(1000);
        ESP.restart();
        return;
    }
    ESP_LOGI("main", "app setup task=%s core=%d", pcTaskGetName(nullptr), xPortGetCoreID());
    app.begin();
    Logger::checkpoint("running");
}

void loop() {
    const uint32_t now = millis();
    app.update(now);
}
