#pragma once
#include <cstddef>
#include <cstdint>
inline uint32_t testClockMs = 0;
inline unsigned testReadDelays = 0;
inline uint32_t millis() { return testClockMs; }
inline void delay(uint32_t ms) { testClockMs += ms; }
inline void delayMicroseconds(unsigned us) { if (us == 50) ++testReadDelays; }
