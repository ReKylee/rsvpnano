#include "ui/screens/ScreenCommon.h"

#include <WiFi.h>

#include <algorithm>
#include <functional>

#include "net/WifiConnection.h"
#include "settings/SettingsStore.h"

namespace screens {

    void NetworkScreen::begin(settings::SettingsStore& store) {
        const auto& persisted = store.settings();
        ssid = persisted.network.wifiSsid;
        password_ = store.secrets().wifiPassword;
        owner = persisted.updates.repositoryOwner;
        tag = persisted.updates.releaseTag;
        automatic = persisted.updates.automatic;
        ssidStored = !ssid.empty();
        autoCheckPending = automatic && !ssid.empty();
    }

    void NetworkScreen::draw(ui::Context& ui, settings::SettingsStore& store, Screen& screen) {
        const ui::Rect content = detail::content(ui);
        if (ui.button({content.x, content.y, 64, 24}, ui.text(UiText::Back)))
            screen = Screen::Settings;
        ui.label({static_cast<int16_t>(content.x + 74), content.y, static_cast<int16_t>(content.w - 74), 24},
                 ui.text(UiText::NetworkUpdates), 2);

        constexpr int16_t gap = 6;
        const int16_t sectionY = static_cast<int16_t>(content.y + 30);
        ui.separator({content.x, sectionY, content.w, 10}, ui.text(UiText::ConnectionReleaseSection));
        const int16_t firstRowY = static_cast<int16_t>(sectionY + 14);
        const int16_t networkWidth = static_cast<int16_t>((content.w - gap) * 2 / 3);
        if (ui.setting({content.x, firstRowY, networkWidth, 32}, ui.text(UiText::Network),
                       ssid.empty() ? ui.text(UiText::NotSet) : std::string_view{ssid},
                       ui::SettingLayout::Inline)) {
            openWifiScan();
            screen = Screen::WifiScan;
        }
        if (ui.toggle({static_cast<int16_t>(content.x + networkWidth + gap), firstRowY,
                       static_cast<int16_t>(content.w - networkWidth - gap), 32},
                      ui.text(UiText::AutomaticChecks), automatic)) {
            automatic = !automatic;
            store.settings().updates.automatic = automatic;
            store.acceptChanges();
            autoCheckPending = automatic && !ssid.empty();
        }
        const int16_t secondRowY = static_cast<int16_t>(firstRowY + 38);
        const int16_t halfWidth = static_cast<int16_t>((content.w - gap) / 2);
        if (ui.setting({content.x, secondRowY, halfWidth, 32}, ui.text(UiText::OtaOwner),
                       owner.empty() ? ui.text(UiText::Default) : std::string_view{owner})) {
            editField_ = EditField::Owner;
            editValue_ = owner;
            keyboard_ = {};
            screen = Screen::NetworkEdit;
        }
        if (ui.setting({static_cast<int16_t>(content.x + halfWidth + gap), secondRowY, halfWidth, 32},
                       ui.text(UiText::ReleaseTag),
                       tag.empty() ? ui.text(UiText::Latest) : std::string_view{tag})) {
            editField_ = EditField::Tag;
            editValue_ = tag;
            keyboard_ = {};
            screen = Screen::NetworkEdit;
        }

        ui::Grid actions{{content.x, static_cast<int16_t>(sectionY + 88), content.w,
                          static_cast<int16_t>(content.h - 88)}, static_cast<uint8_t>(ssidStored ? 3 : 2), 36, gap};
        if (ui.button(actions.next(), ui.text(UiText::CompanionSetup)))
            screen = Screen::Sync;
        if (ui.button(actions.next(), ui.text(UiText::FirmwareUpdates)))
            screen = Screen::Ota;
        if (ssidStored && ui.button(actions.next(), ui.text(UiText::ForgetNetwork))) {
            ssid.clear();
            password_.clear();
            saveNetwork(store);
            ssidStored = false;
            autoCheckPending = false;
        }
    }

    void NetworkScreen::openWifiScan() {
        closeWifi();
        networkCount_ = 0;
        scanState_ = WifiScanState::Idle;
    }

    void NetworkScreen::closeWifi() {
        WiFi.scanDelete();
        net::disconnect();
    }

    void NetworkScreen::drawWifiScan(ui::Context& ui, settings::SettingsStore& store, Screen& screen) {
        const ui::Rect content = detail::content(ui);
        if (ui.button({content.x, content.y, 64, 24}, ui.text(UiText::Back))) {
            closeWifi();
            screen = Screen::NetworkSettings;
            return;
        }
        ui.label({static_cast<int16_t>(content.x + 74), content.y, static_cast<int16_t>(content.w - 74), 24},
                 ui.text(UiText::WifiNetworks), 2);

        if (scanState_ == WifiScanState::Idle) {
            WiFi.mode(WIFI_STA);
            scanState_ = WiFi.scanNetworks(true) == WIFI_SCAN_RUNNING ? WifiScanState::Scanning
                                                                      : WifiScanState::Failed;
        }
        if (scanState_ == WifiScanState::Scanning) {
            const int16_t found = WiFi.scanComplete();
            if (found == WIFI_SCAN_FAILED) {
                scanState_ = WifiScanState::Failed;
            } else if (found >= 0) {
                for (int16_t index = 0; index < found; ++index) {
                    const String foundSsid = WiFi.SSID(index);
                    if (foundSsid.isEmpty())
                        continue;
                    const std::string candidate{foundSsid.c_str(), foundSsid.length()};
                    const int32_t rssi = WiFi.RSSI(index);
                    const bool secured = WiFi.encryptionType(index) != WIFI_AUTH_OPEN;
                    const auto activeNetworks = std::span{networks_}.first(networkCount_);
                    const auto existing = std::ranges::find(activeNetworks, candidate, &WifiNetwork::ssid);
                    if (existing == activeNetworks.end()) {
                        if (networkCount_ < networks_.size()) {
                            networks_[networkCount_] = {candidate, rssi, secured};
                            ++networkCount_;
                        } else {
                            const auto weakest = std::ranges::min_element(networks_, std::ranges::less{},
                                                                          &WifiNetwork::rssi);
                            if (rssi > weakest->rssi)
                                *weakest = {candidate, rssi, secured};
                        }
                    } else if (rssi > existing->rssi) {
                        existing->rssi = rssi;
                        existing->secured = secured;
                    }
                }
                std::ranges::sort(std::span{networks_}.first(networkCount_), std::ranges::greater{},
                                  &WifiNetwork::rssi);
                WiFi.scanDelete();
                scanState_ = WifiScanState::Complete;
            }
        }

        const ui::Rect list{content.x, static_cast<int16_t>(content.y + 30), content.w,
                            static_cast<int16_t>(content.h - 30)};
        if (scanState_ == WifiScanState::Idle || scanState_ == WifiScanState::Scanning) {
            ui.label(list, ui.text(UiText::ScanningNetworks), 2, ui::themes::ColorRole::Muted,
                     ui::TextAlign::Center);
            return;
        }
        if (scanState_ == WifiScanState::Failed || networkCount_ == 0) {
            ui::Column column{list, 8};
            ui.label(column.next(32),
                     ui.text(scanState_ == WifiScanState::Failed ? UiText::ScanFailed : UiText::NoNetworksFound), 2,
                     ui::themes::ColorRole::Muted, ui::TextAlign::Center);
            if (ui.button(column.next(34), ui.text(UiText::Retry)))
                openWifiScan();
            return;
        }

        ui::Grid grid{list, 2, 30, 2};
        for (size_t index = 0; index < networkCount_; ++index) {
            const WifiNetwork& network = networks_[index];
            const std::string signal = std::to_string(network.rssi) + " dBm";
            if (ui.setting(grid.next(), network.ssid, signal, ui::SettingLayout::Inline)) {
                const bool savedNetwork = network.ssid == ssid;
                ssid = network.ssid;
                if (!network.secured) {
                    password_.clear();
                    saveNetwork(store);
                    ssidStored = true;
                    closeWifi();
                    screen = Screen::NetworkSettings;
                    return;
                }
                if (!savedNetwork)
                    password_.clear();
                keyboard_ = {};
                connectionFailed_ = false;
                screen = Screen::WifiConnect;
                return;
            }
        }
    }

    bool NetworkScreen::drawWifiConnect(ui::Context& ui, settings::SettingsStore& store, Screen& screen) {
        const ui::Rect content = detail::content(ui);
        const std::string_view label = connectionFailed_ ? ui.text(UiText::ConnectionFailed) : std::string_view{ssid};
        const ui::KeyboardAction action = ui.keyboard(content, password_, 63, keyboard_, label, true);
        if (action == ui::KeyboardAction::Cancel) {
            screen = Screen::WifiScan;
            return false;
        }
        if (action != ui::KeyboardAction::Submit)
            return false;

        ui.endFrame();
        status(ui, ui.text(UiText::Connecting), ssid);
        const auto connected = net::connectStation(ssid.c_str(), password_.c_str(), [&](int percent) {
            status(ui, ui.text(UiText::Connecting), ssid, {}, percent);
        });
        net::disconnect();
        if (!connected) {
            connectionFailed_ = true;
            return true;
        }
        saveNetwork(store);
        ssidStored = true;
        screen = Screen::NetworkSettings;
        return true;
    }

    void NetworkScreen::drawEdit(ui::Context& ui, settings::SettingsStore& store, Screen& screen) {
        const ui::Rect content = detail::content(ui);
        const ui::KeyboardAction action =
            ui.keyboard(content, editValue_, 63, keyboard_,
                        ui.text(editField_ == EditField::Owner ? UiText::OtaOwner : UiText::ReleaseTag));
        if (action == ui::KeyboardAction::Cancel) {
            screen = Screen::NetworkSettings;
        } else if (action == ui::KeyboardAction::Submit) {
            if (editField_ == EditField::Owner) {
                owner = editValue_;
                store.settings().updates.repositoryOwner = owner;
            } else {
                tag = editValue_;
                store.settings().updates.releaseTag = tag;
            }
            store.acceptChanges();
            screen = Screen::NetworkSettings;
        }
    }

    void NetworkScreen::saveNetwork(settings::SettingsStore& store) {
        store.settings().network.wifiSsid = ssid;
        store.secrets().wifiPassword = password_;
        store.acceptChanges();
        store.acceptSecretChanges();
    }

} // namespace screens
