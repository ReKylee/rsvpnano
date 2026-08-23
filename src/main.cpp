#include <Arduino.h>
#include <esp_log.h>
#include "logging/Logger.h"

#include "app/App.h"
#if RSVP_BENCHMARK_MODE
#include "benchmark/BenchmarkRunner.h"
#endif
#include "board/Board.h"
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
#if RSVP_BENCHMARK_MODE
    ESP_LOGI("main", "benchmark setup");
    Benchmark::run();
#else
    ESP_LOGI("main", "app setup task=%s core=%d", pcTaskGetName(nullptr), xPortGetCoreID());
    app.begin();
#endif
}

void loop() {
#if RSVP_BENCHMARK_MODE
    delay(1000);
#else
    const uint32_t now = millis();
    app.update(now);
#endif
}
