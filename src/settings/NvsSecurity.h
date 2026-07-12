#pragma once

#include <cstdint>

class Preferences;

namespace fs {
    class FS;
}

namespace settings {

    enum class NvsEncryptionState : uint8_t {
        Available,
        Enabled,
        Unavailable,
    };

    NvsEncryptionState nvsEncryptionState();
    bool initializeNvsEncryption();
    bool enableNvsEncryption(Preferences& preferences, fs::FS& filesystem);

} // namespace settings
