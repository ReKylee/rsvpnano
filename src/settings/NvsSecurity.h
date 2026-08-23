#pragma once

#include <cstdint>

class Preferences;

#include "settings/SettingsStore.h"

namespace fs {
    class FS;
}

namespace settings {

    inline constexpr char kStateNvsNamespace[] = "rsvp";

    enum class NvsEncryptionState : uint8_t {
        Available,
        Enabled,
        Unavailable,
    };

    NvsEncryptionState nvsEncryptionState();
    bool initializeNvsEncryption();
    bool enableNvsEncryption(Preferences& statePreferences, SettingsStore& settingsStore);

} // namespace settings
