#include "companion/http/CompanionApi.h"

#include <utility>

#include "board/BoardStorage.h"
#include "logging/Logger.h"
#include "storage/fs/StoragePaths.h"
#include "timer/FocusTimerStorage.h"

auto CompanionApi::getFocusTimers(httpd_req_t& request) -> companion::api::Result<std::span<const focus::Timer>> {
    (void) request;
    return std::span<const focus::Timer>{focusScreen_.timers().timers};
}

companion::api::Result<> CompanionApi::putFocusTimers(httpd_req_t& request) {
    return readJson<focus::Timers>(request, focus::kMaxConfigBytes, "Focus timer payload exceeds 4 KB")
        .and_then([this](focus::Timers timers) -> companion::api::Result<> {
            if (!focus::valid(timers)) {
                return std::unexpected(companion::api::httpError(HTTP_CODE_UNPROCESSABLE_ENTITY, "invalid_focus_timers",
                                                                 "Focus timers are invalid", "timers"));
            }
            if (auto saved = focus::save(Board::Storage::filesystem(), timers); !saved) {
                Logger::failure("companion", "save focus timers", StoragePaths::kFocusConfigPath, saved.error());
                return std::unexpected(companion::api::httpError(HTTP_CODE_INTERNAL_SERVER_ERROR, "focus_save_failed",
                                                                 saved.error().message()));
            }
            focusScreen_.setTimers(std::move(timers));
            return {};
        });
}
