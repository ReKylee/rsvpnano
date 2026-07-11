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
                 "Network & updates", 2);

        const int16_t gap = 6;
        ui::Grid grid{{content.x, static_cast<int16_t>(content.y + 32), content.w,
                       static_cast<int16_t>(content.h - 32)}, 3, 34, gap};
        if (ui.setting(grid.next(), "Network", ssid.empty() ? "Not set" : std::string_view{ssid}))
            screen = Screen::Sync;
        if (ui.toggle(grid.next(), "Automatic checks", automatic)) {
            automatic = !automatic;
            settings::save<settings::prefs::OtaAuto>(preferences, automatic);
            autoCheckPending = automatic && !ssid.empty();
        }
        if (ui.setting(grid.next(), "OTA owner", owner.empty() ? "Default" : std::string_view{owner}))
            screen = Screen::Sync;
        if (ui.setting(grid.next(), "Release tag", tag.empty() ? "Latest" : std::string_view{tag}))
            screen = Screen::Sync;
        if (ui.button(grid.next(), "Companion setup"))
            screen = Screen::Sync;
        if (ui.button(grid.next(), "Firmware updates"))
            screen = Screen::Ota;
        if (ssidStored && ui.button(grid.next(), "Forget network")) {
            preferences.remove(settings::prefs::WifiSsid::key());
            preferences.remove(settings::prefs::WifiPassword::key());
            ssid.clear();
            ssidStored = false;
            autoCheckPending = false;
        }
    }

} // namespace screens
