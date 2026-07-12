#include "ui/screens/ScreenCommon.h"

#include <WiFi.h>

#include <algorithm>

#include "net/WifiConnection.h"
#include "settings/PreferenceSpecs.h"
#include "update/OtaUpdater.h"

namespace screens {

    void NetworkScreen::begin(Preferences& preferences) {
        const OtaUpdater::Config config = OtaUpdater{}.config(preferences);
        ssid.assign(config.wifiSsid.c_str(), config.wifiSsid.length());
        password_.assign(config.wifiPassword.c_str(), config.wifiPassword.length());
        owner.assign(config.githubOwner.c_str(), config.githubOwner.length());
        tag.assign(config.githubTag.c_str(), config.githubTag.length());
        automatic = config.autoCheck;
        ssidStored = preferences.isKey(settings::prefs::WifiSsid::key());
        autoCheckPending = automatic && !ssid.empty();
    }

    void NetworkScreen::draw(ui::Context& ui, Preferences& preferences, Screen& screen) {
        const ui::Rect content = detail::content(ui);
        if (ui.button({content.x, content.y, 64, 24}, ui.text(UiText::Back)))
            screen = Screen::Settings;
        ui.label({static_cast<int16_t>(content.x + 74), content.y, static_cast<int16_t>(content.w - 74), 24},
                 ui.text(UiText::NetworkUpdates), 2);

        constexpr int16_t gap = 6;
        const int16_t sectionY = static_cast<int16_t>(content.y + 30);
        ui.separator({content.x, sectionY, content.w, 10}, ui.text(UiText::ConnectionReleaseSection));
        ui::Grid grid{{content.x, static_cast<int16_t>(sectionY + 14), content.w,
                       static_cast<int16_t>(content.h - 44)}, 2, 32, gap};
        if (ui.setting(grid.next(), ui.text(UiText::Network),
                       ssid.empty() ? ui.text(UiText::NotSet) : std::string_view{ssid})) {
            openWifiScan();
            screen = Screen::WifiScan;
        }
        if (ui.toggle(grid.next(), ui.text(UiText::AutomaticChecks), automatic)) {
            automatic = !automatic;
            settings::save<settings::prefs::OtaAuto>(preferences, automatic);
            autoCheckPending = automatic && !ssid.empty();
        }
        if (ui.setting(grid.next(), ui.text(UiText::OtaOwner),
                       owner.empty() ? ui.text(UiText::Default) : std::string_view{owner})) {
            editField_ = EditField::Owner;
            editValue_ = owner;
            keyboard_ = {};
            screen = Screen::NetworkEdit;
        }
        if (ui.setting(grid.next(), ui.text(UiText::ReleaseTag),
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
            preferences.remove(settings::prefs::WifiSsid::key());
            preferences.remove(settings::prefs::WifiPassword::key());
            ssid.clear();
            ssidStored = false;
            autoCheckPending = false;
        }
    }

    void NetworkScreen::openWifiScan() {
        closeWifi();
        networkCount_ = 0;
        scanStarted_ = false;
        scanFinished_ = false;
        scanFailed_ = false;
    }

    void NetworkScreen::closeWifi() {
        WiFi.scanDelete();
        net::disconnect();
    }

    void NetworkScreen::drawWifiScan(ui::Context& ui, Preferences& preferences, Screen& screen) {
        const ui::Rect content = detail::content(ui);
        if (ui.button({content.x, content.y, 64, 24}, ui.text(UiText::Back))) {
            closeWifi();
            screen = Screen::NetworkSettings;
            return;
        }
        ui.label({static_cast<int16_t>(content.x + 74), content.y, static_cast<int16_t>(content.w - 74), 24},
                 ui.text(UiText::WifiNetworks), 2);

        if (!scanStarted_) {
            WiFi.mode(WIFI_STA);
            scanStarted_ = WiFi.scanNetworks(true) == WIFI_SCAN_RUNNING;
            scanFailed_ = !scanStarted_;
            scanFinished_ = scanFailed_;
        }
        if (scanStarted_ && !scanFinished_) {
            const int16_t found = WiFi.scanComplete();
            if (found == WIFI_SCAN_FAILED) {
                scanFailed_ = true;
                scanFinished_ = true;
            } else if (found >= 0) {
                for (int16_t index = 0; index < found && networkCount_ < networks_.size(); ++index) {
                    const String foundSsid = WiFi.SSID(index);
                    if (foundSsid.isEmpty())
                        continue;
                    const std::string candidate{foundSsid.c_str(), foundSsid.length()};
                    if (std::find(networks_.begin(), networks_.begin() + networkCount_, candidate)
                        == networks_.begin() + networkCount_) {
                        networks_[networkCount_] = candidate;
                        securedNetworks_[networkCount_] = WiFi.encryptionType(index) != WIFI_AUTH_OPEN;
                        ++networkCount_;
                    }
                }
                WiFi.scanDelete();
                scanFinished_ = true;
            }
        }

        const ui::Rect list{content.x, static_cast<int16_t>(content.y + 30), content.w,
                            static_cast<int16_t>(content.h - 30)};
        if (!scanFinished_) {
            ui.label(list, ui.text(UiText::ScanningNetworks), 2, ui::themes::ColorRole::Muted,
                     ui::TextAlign::Center);
            return;
        }
        if (scanFailed_ || networkCount_ == 0) {
            ui::Column column{list, 8};
            ui.label(column.next(32), ui.text(scanFailed_ ? UiText::ScanFailed : UiText::NoNetworksFound), 2,
                     ui::themes::ColorRole::Muted, ui::TextAlign::Center);
            if (ui.button(column.next(34), ui.text(UiText::Retry)))
                openWifiScan();
            return;
        }

        ui::Grid grid{list, 2, 27, 5};
        for (size_t index = 0; index < networkCount_; ++index) {
            if (ui.button(grid.next(), networks_[index])) {
                const bool savedNetwork = networks_[index] == ssid;
                ssid = networks_[index];
                if (!securedNetworks_[index]) {
                    password_.clear();
                    settings::save<settings::prefs::WifiSsid>(preferences, ssid);
                    settings::save<settings::prefs::WifiPassword>(preferences, password_);
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

    bool NetworkScreen::drawWifiConnect(ui::Context& ui, Preferences& preferences, Screen& screen) {
        const ui::Rect content = detail::content(ui);
        ui.label({content.x, content.y, content.w, 22},
                 connectionFailed_ ? ui.text(UiText::ConnectionFailed) : std::string_view{ssid}, 2,
                 connectionFailed_ ? ui::themes::ColorRole::Accent : ui::themes::ColorRole::Foreground,
                 ui::TextAlign::Center);
        const ui::KeyboardAction action =
            ui.keyboard({content.x, static_cast<int16_t>(content.y + 24), content.w,
                         static_cast<int16_t>(content.h - 24)},
                        password_, 63, keyboard_, true);
        if (action == ui::KeyboardAction::Cancel) {
            screen = Screen::WifiScan;
            return false;
        }
        if (action != ui::KeyboardAction::Submit)
            return false;

        ui.endFrame();
        status(ui, ui.text(UiText::Connecting), ssid);
        const bool connected = net::connectStation(ssid.c_str(), password_.c_str(), [&](int percent) {
            status(ui, ui.text(UiText::Connecting), ssid, {}, percent);
        });
        net::disconnect();
        if (!connected) {
            connectionFailed_ = true;
            return true;
        }
        settings::save<settings::prefs::WifiSsid>(preferences, ssid);
        settings::save<settings::prefs::WifiPassword>(preferences, password_);
        ssidStored = true;
        screen = Screen::NetworkSettings;
        return true;
    }

    void NetworkScreen::drawEdit(ui::Context& ui, Preferences& preferences, Screen& screen) {
        const ui::Rect content = detail::content(ui);
        ui.label({content.x, content.y, content.w, 22},
                 ui.text(editField_ == EditField::Owner ? UiText::OtaOwner : UiText::ReleaseTag), 2,
                 ui::themes::ColorRole::Foreground, ui::TextAlign::Center);
        const ui::KeyboardAction action =
            ui.keyboard({content.x, static_cast<int16_t>(content.y + 24), content.w,
                         static_cast<int16_t>(content.h - 24)},
                        editValue_, 63, keyboard_);
        if (action == ui::KeyboardAction::Cancel) {
            screen = Screen::NetworkSettings;
        } else if (action == ui::KeyboardAction::Submit) {
            if (editField_ == EditField::Owner) {
                owner = editValue_;
                settings::save<settings::prefs::OtaOwner>(preferences, owner);
            } else {
                tag = editValue_;
                settings::save<settings::prefs::OtaTag>(preferences, tag);
            }
            screen = Screen::NetworkSettings;
        }
    }

} // namespace screens
