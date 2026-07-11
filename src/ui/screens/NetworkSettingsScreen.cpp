#include "ui/screens/ScreenCommon.h"

#include "settings/PreferenceSpecs.h"
#include "update/OtaUpdater.h"

namespace screens {

    void NetworkScreen::begin(Preferences& preferences) {
        const OtaUpdater::Config config = OtaUpdater{}.config(preferences);
        ssid.assign(config.wifiSsid.c_str(), config.wifiSsid.length());
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
                       ssid.empty() ? ui.text(UiText::NotSet) : std::string_view{ssid}))
            screen = Screen::Sync;
        if (ui.toggle(grid.next(), ui.text(UiText::AutomaticChecks), automatic)) {
            automatic = !automatic;
            settings::save<settings::prefs::OtaAuto>(preferences, automatic);
            autoCheckPending = automatic && !ssid.empty();
        }
        if (ui.setting(grid.next(), ui.text(UiText::OtaOwner),
                       owner.empty() ? ui.text(UiText::Default) : std::string_view{owner}))
            screen = Screen::Sync;
        if (ui.setting(grid.next(), ui.text(UiText::ReleaseTag),
                       tag.empty() ? ui.text(UiText::Latest) : std::string_view{tag}))
            screen = Screen::Sync;

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

} // namespace screens
