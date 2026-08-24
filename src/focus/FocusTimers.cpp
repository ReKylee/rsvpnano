#include "focus/FocusTimers.h"

#include <glaze/toml.hpp>

#include <algorithm>
#include <utility>

#include "settings/SettingsGlaze.h"
#include "text/Utf8Text.h"

namespace focus {
    namespace {
        bool validTimerName(std::string_view text) {
            uint32_t codepoint = 0;
            while (!text.empty()) {
                if (!Utf8Text::decode(text, codepoint) || codepoint < 0x20U || codepoint == 0x7FU
                    || (codepoint >= 0x80U && codepoint <= 0x9FU))
                    return false;
            }
            return true;
        }

    } // namespace

    Timer defaultTimer() {
        Timer timer;
        timer.name = "Pomodoro";
        return timer;
    }

    Timers defaultTimers() {
        Timers timers;
        timers.timers.push_back(defaultTimer());
        return timers;
    }

    bool valid(const Timer& timer) {
        return !timer.name.empty() && timer.name.size() <= kMaxTimerNameBytes && validTimerName(timer.name);
    }

    bool valid(const Timers& timers) {
        return !timers.timers.empty() && timers.timers.size() <= kMaxTimers
            && std::ranges::all_of(timers.timers, [](const Timer& timer) {
                   return valid(timer);
               });
    }

    std::expected<Timers, std::error_code> decodeToml(std::string_view content) {
        if (content.empty() || content.size() > kMaxConfigBytes)
            return std::unexpected(std::make_error_code(content.empty() ? std::errc::invalid_argument
                                                                        : std::errc::value_too_large));

        Timers parsed;
        if (glz::read<glz::opts{.format = glz::TOML, .error_on_unknown_keys = false}>(parsed, content))
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        if (parsed.timers.empty())
            parsed.timers.push_back(defaultTimer());
        if (parsed.timers.size() > kMaxTimers)
            parsed.timers.resize(kMaxTimers);
        if (!valid(parsed))
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        return parsed;
    }

    std::expected<std::string, std::error_code> encodeToml(const Timers& timers) {
        if (!valid(timers))
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        std::string output;
        if (glz::write_toml(timers, output))
            return std::unexpected(std::make_error_code(std::errc::io_error));
        return output;
    }

} // namespace focus
