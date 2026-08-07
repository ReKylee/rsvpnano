#pragma once

#include <Preferences.h>

#include <cstdint>

#include "settings/SettingsCodec.h"

namespace fs {
    class FS;
}

namespace settings {

    inline constexpr char kSettingsNvsNamespace[] = "rsvp_cfg2";
    inline constexpr char kSettingsNvsKey[] = "settings";

    class SettingsStore {
    public:
        ~SettingsStore();

        SettingsResult<> begin(fs::FS* filesystem);

        DeviceSettings& settings() {
            return settings_;
        }
        const DeviceSettings& settings() const {
            return settings_;
        }
        DeviceSecrets& secrets() {
            return secrets_;
        }
        const DeviceSecrets& secrets() const {
            return secrets_;
        }

        SettingsResult<> acceptChanges();
        SettingsResult<> acceptSecretChanges();
        SettingsResult<> replace(DeviceSettings candidate, SettingsSource source);
        void update(uint32_t nowMs);
        SettingsResult<> flush();
        void closeNvs();
        SettingsResult<> reopenNvsAndPersist();

        bool sdMirrorEnabled() const {
            return mirrorEnabled_;
        }

    private:
        SettingsResult<> writeSettings(std::string_view canonicalToml);
        SettingsResult<> writeSecrets();
        SettingsResult<> writeFile(std::string_view content);

        Preferences preferences_;
        fs::FS* filesystem_ = nullptr;
        DeviceSettings settings_;
        DeviceSettings lastAccepted_;
        DeviceSecrets secrets_;
        DeviceSecrets lastAcceptedSecrets_;
        bool nvsOpen_ = false;
        bool mirrorEnabled_ = false;
        bool dirty_ = false;
        bool secretsDirty_ = false;
        uint32_t dirtyAtMs_ = 0;
    };

} // namespace settings
