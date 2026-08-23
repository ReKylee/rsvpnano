#pragma once

#include <cstdint>

#include "settings/SettingsModel.h"

namespace reading {

    struct BookIdentity {
        uint32_t sourceSize = 0;
        uint32_t sourceFingerprint = 0;
        uint32_t wordCount = 0;

        bool operator==(const BookIdentity&) const = default;
    };

    struct State {
        uint32_t wordIndex = 0;
        settings::ReadingOverrides overrides;

        bool operator==(const State&) const = default;
    };

    struct StoredState {
        BookIdentity identity;
        State reading;

        bool operator==(const StoredState&) const = default;
    };

} // namespace reading
