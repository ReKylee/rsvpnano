#pragma once

// ESP newlib exposes the POSIX timezone offset as _timezone.
#if defined(ARDUINO_ARCH_ESP32)
#define timezone _timezone
#endif
