#pragma once

#include <expected>
#include <system_error>

#include "focus/FocusTimers.h"

namespace fs {
    class FS;
}

namespace focus {

    std::expected<Timers, std::error_code> load(fs::FS& filesystem);
    std::expected<void, std::error_code> save(fs::FS& filesystem, const Timers& timers);

} // namespace focus
