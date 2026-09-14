#pragma once

#include <cstddef>
#include "app/screens/Navigation.h"
#include "settings/NvsSecurity.h"
#include "ui/Ui.h"

namespace screens {

    Action device(ui::Context& ui, bool storageReady, size_t bookCount, settings::NvsEncryptionState encryptionState,
                  Screen& screen);
    Action storageEncryption(ui::Context& ui, settings::NvsEncryptionState encryptionState, Screen& screen);

} // namespace screens
