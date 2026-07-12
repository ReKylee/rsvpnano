#pragma once

#include <cstdint>

class Preferences;

namespace fs {
    class FS;
}

namespace settings {

    void markDirty();
    bool configMirrorReady();
    bool reconcile(Preferences& preferences, fs::FS& filesystem);
    void update(Preferences& preferences, fs::FS& filesystem, uint32_t nowMs);
    bool flush(Preferences& preferences, fs::FS& filesystem);

} // namespace settings
