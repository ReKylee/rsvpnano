#pragma once

#include <cstdint>

#include "timer/FocusTimers.h"

namespace fs {
    class FS;
}

namespace focus {

    enum class LoadResult : uint8_t {
        Missing,
        Valid,
        Invalid,
    };

    LoadResult load(fs::FS& filesystem, Timers& timers);
    bool save(fs::FS& filesystem, const Timers& timers);

} // namespace focus
