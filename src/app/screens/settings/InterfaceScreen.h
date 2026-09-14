#pragma once

#include <span>
#include "app/screens/Navigation.h"
#include "themes/ThemeStore.h"
#include "settings/SettingsModel.h"
#include "ui/Ui.h"

namespace screens {

    class InterfaceScreen {
    public:
        ThemeStore themes;

        bool begin(ui::Context& ui, settings::InterfaceSettings& settings, const locales::Catalog& languages,
                   void (*setBrightness)(uint8_t));
        bool draw(ui::Context& ui, settings::InterfaceSettings& settings, std::span<const uint32_t> standbyDurations,
                  void (*setBrightness)(uint8_t), Screen& screen);

    private:
        void nextTheme(ui::Context& ui, settings::InterfaceSettings& config);
        void nextLocale(ui::Context& ui, settings::InterfaceSettings& config);
        const locales::Catalog* languages_ = nullptr;
    };

} // namespace screens
