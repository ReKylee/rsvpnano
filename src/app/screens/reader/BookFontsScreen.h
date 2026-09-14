#pragma once

#include "app/screens/Navigation.h"
#include "ui/Ui.h"
#include "fonts/FontCatalog.h"
#include "library/BookMetadata.h"
#include "settings/SettingsModel.h"

namespace screens {

    bool bookFonts(ui::Context& ui, const BookMetadata& metadata, settings::ReadingOverrides& overrides,
                   const locales::Catalog& localeCatalog, FontCatalog& fonts, Screen& screen);

} // namespace screens
