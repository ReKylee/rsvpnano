#pragma once

#include <array>
#include <string>
#include "app/screens/Navigation.h"
#include "settings/SettingsStore.h"
#include "ui/Ui.h"

namespace screens {

    class NetworkScreen {
    public:
        bool startupCheckPending = false;

        void begin(settings::SettingsStore& store);
        Action draw(ui::Context& ui, settings::SettingsStore& store, Screen& screen);
        void openWifiScan();
        void closeWifi();
        void drawWifiScan(ui::Context& ui, settings::SettingsStore& store, Screen& screen);
        bool drawWifiConnect(ui::Context& ui, settings::SettingsStore& store, Screen& screen);
        void drawEdit(ui::Context& ui, settings::SettingsStore& store, Screen& screen);

    private:
        struct WifiNetwork {
            std::string ssid;
            int32_t rssi = 0;
            bool secured = false;
        };

        enum class WifiScanState : uint8_t {
            Idle,
            Scanning,
            Complete,
            Failed,
        };

        enum class EditField : uint8_t {
            Owner,
            Tag,
        };

        void saveNetwork(settings::SettingsStore& store, std::string_view ssid);
        void updateWifiScan();

        std::array<WifiNetwork, 8> networks_;
        size_t networkCount_ = 0;
        size_t selectedNetworkIndex_ = networks_.size();
        std::string password_;
        std::string editValue_;
        ui::KeyboardState keyboard_;
        EditField editField_ = EditField::Owner;
        WifiScanState scanState_ = WifiScanState::Idle;
        bool connectionFailed_ = false;
    };

} // namespace screens
