#include "settings/NvsSecurity.h"
#include <esp_log.h>

#include <Arduino.h>
#include <Preferences.h>
#include <esp_efuse.h>
#include <esp_efuse_chip.h>
#include <esp_system.h>
#include <hal/hmac_types.h>
#include <nvs_flash.h>
#include <nvs_sec_provider.h>

namespace settings {
    namespace {

        constexpr esp_efuse_block_t kNvsKeyBlock = EFUSE_BLK_KEY5;
        constexpr hmac_key_id_t kNvsHmacKey = HMAC_KEY5;

        esp_err_t earlyInitializationResult = ESP_OK;

        enum class EncryptionState : uint8_t {
            Plaintext,
            Encrypted,
            KeyConflict,
        };

        EncryptionState encryptionState() {
            if (esp_efuse_key_block_unused(kNvsKeyBlock))
                return EncryptionState::Plaintext;
            return esp_efuse_get_key_purpose(kNvsKeyBlock) == ESP_EFUSE_KEY_PURPOSE_HMAC_UP
                     ? EncryptionState::Encrypted
                     : EncryptionState::KeyConflict;
        }

        esp_err_t securityConfig(nvs_sec_cfg_t& config, bool generate) {
            nvs_sec_scheme_t* scheme = nullptr;
            const nvs_sec_config_hmac_t schemeConfig{.hmac_key_id = kNvsHmacKey};
            esp_err_t result = nvs_sec_provider_register_hmac(&schemeConfig, &scheme);
            if (result == ESP_OK) {
                result = generate ? nvs_flash_generate_keys_v2(scheme, &config)
                                  : nvs_flash_read_security_cfg_v2(scheme, &config);
                nvs_sec_provider_deregister(scheme);
            }
            return result;
        }

        void initializeEncryptedNvsBeforeArduino() __attribute__((constructor));

        void initializeEncryptedNvsBeforeArduino() {
            // Arduino initializes plaintext NVS before setup(), so encrypted storage must exist before app_main().
            if (encryptionState() != EncryptionState::Encrypted)
                return;

            nvs_sec_cfg_t config{};
            earlyInitializationResult = securityConfig(config, false);
            if (earlyInitializationResult != ESP_OK)
                return;

            earlyInitializationResult = nvs_flash_secure_init(&config);
            if (earlyInitializationResult != ESP_OK && nvs_flash_erase() == ESP_OK)
                earlyInitializationResult = nvs_flash_secure_init(&config);
        }

        void restorePlaintextNvs(Preferences& statePreferences, SettingsStore& settingsStore) {
            if (nvs_flash_init() != ESP_OK || !statePreferences.begin(kStateNvsNamespace)
                || !settingsStore.reopenNvsAndPersist()) {
                ESP_LOGE("settings", "plaintext NVS recovery failed; restarting");
                delay(250);
                esp_restart();
            }
        }

    } // namespace

    NvsEncryptionState nvsEncryptionState() {
        const EncryptionState state = encryptionState();
        if (state == EncryptionState::Plaintext)
            return NvsEncryptionState::Available;
        if (state == EncryptionState::Encrypted)
            return NvsEncryptionState::Enabled;
        return NvsEncryptionState::Unavailable;
    }

    bool initializeNvsEncryption() {
        const EncryptionState state = encryptionState();
        if (state == EncryptionState::Plaintext)
            return true;
        if (state == EncryptionState::KeyConflict) {
            ESP_LOGI("settings", "eFuse KEY5 is already assigned; NVS encryption disabled");
            return true;
        }

        if (earlyInitializationResult == ESP_OK) {
            ESP_LOGI("settings", "encrypted NVS initialized");
            return true;
        }
        ESP_LOGE("settings", "encrypted NVS initialization failed: %s",
                      esp_err_to_name(earlyInitializationResult));
        return false;
    }

    bool enableNvsEncryption(Preferences& statePreferences, SettingsStore& settingsStore) {
        if (encryptionState() != EncryptionState::Plaintext || !settingsStore.flush())
            return false;

        nvs_sec_scheme_t* scheme = nullptr;
        const nvs_sec_config_hmac_t schemeConfig{.hmac_key_id = kNvsHmacKey};
        esp_err_t result = nvs_sec_provider_register_hmac(&schemeConfig, &scheme);
        if (result != ESP_OK) {
            ESP_LOGE("settings", "NVS encryption provider failed: %s", esp_err_to_name(result));
            return false;
        }

        settingsStore.closeNvs();
        statePreferences.end();
        result = nvs_flash_erase();
        if (result == ESP_OK) {
            nvs_sec_cfg_t config{};
            result = nvs_flash_generate_keys_v2(scheme, &config);
            if (result == ESP_OK)
                result = nvs_flash_secure_init(&config);
        }
        nvs_sec_provider_deregister(scheme);

        if (result == ESP_OK && statePreferences.begin(kStateNvsNamespace) && settingsStore.reopenNvsAndPersist()) {
            ESP_LOGI("settings", "NVS encryption enabled; restarting");
            delay(250);
            esp_restart();
        }

        ESP_LOGE("settings", "NVS encryption migration failed: %s", esp_err_to_name(result));
        if (encryptionState() == EncryptionState::Plaintext)
            restorePlaintextNvs(statePreferences, settingsStore);
        else {
            delay(250);
            esp_restart();
        }
        return false;
    }

} // namespace settings
