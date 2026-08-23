#pragma once

#include <Arduino.h>
#include <ESP_I2S.h>
#include <Wire.h>

namespace BoardDrivers::Es8311 {

    struct Context {
        Context(TwoWire& wire, uint8_t address, i2s_port_t i2sPort, int mclkPin, int bclkPin, int wsPin, int dataOutPin,
                uint32_t sampleRateHz = 16000) :
                wire(wire),
                address(address),
                i2sPort(i2sPort),
                i2s(i2sPort),
                mclkPin(mclkPin),
                bclkPin(bclkPin),
                wsPin(wsPin),
                dataOutPin(dataOutPin),
                sampleRateHz(sampleRateHz) {}

        TwoWire& wire;
        uint8_t address = 0;
        i2s_port_t i2sPort = I2S_NUM_0;
        I2SClass i2s;
        int mclkPin = -1;
        int bclkPin = -1;
        int wsPin = -1;
        int dataOutPin = -1;
        uint32_t sampleRateHz = 16000;
        bool available = false;
        bool i2sInitialized = false;
    };

    bool begin(Context& context);
    bool prepareOutput(Context& context);
    bool recoverOutputPath(Context& context);
    bool writeSamples(Context& context, const int16_t* samples, size_t sampleCount, uint32_t timeoutMs);
    bool available(const Context& context);

} // namespace BoardDrivers::Es8311
