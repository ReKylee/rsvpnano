#include "companion/CompanionApi.h"

#include <system_error>
#include <utility>

#include "board/BoardStorage.h"
#include "logging/Logger.h"
#include "storage/fs/StoragePaths.h"
#include "timer/FocusTimerStorage.h"

namespace {

    namespace api = companion::api;

    [[nodiscard]] api::Result<focus::Timers> readFocusTimers() {
        return focus::load(Board::Storage::filesystem())
            .or_else([](std::error_code error) -> std::expected<focus::Timers, std::error_code> {
                if (error == std::errc::no_such_file_or_directory)
                    return focus::defaultTimers();
                return std::unexpected(error);
            })
            .transform_error([](std::error_code error) {
                return api::httpError(HTTP_CODE_INTERNAL_SERVER_ERROR, "focus_load_failed", error.message());
            });
    }

} // namespace

auto CompanionApi::getFocusTimers(httpd_req_t& request) -> companion::api::Result<FocusTimerList> {
    (void) request;
    return readFocusTimers().transform([](focus::Timers timers) {
        return std::move(timers.timers);
    });
}

companion::api::Result<> CompanionApi::putFocusTimers(httpd_req_t& request) {
    return readJson<focus::Timers>(request, focus::kMaxConfigBytes, "Focus timer payload exceeds 4 KB")
        .and_then([this](focus::Timers timers) -> companion::api::Result<> {
            if (!focus::valid(timers)) {
                return std::unexpected(companion::api::httpError(HTTP_CODE_UNPROCESSABLE_ENTITY,
                                                                 "invalid_focus_timers", "Focus timers are invalid",
                                                                 "timers"));
            }
            if (auto saved = focus::save(Board::Storage::filesystem(), timers); !saved) {
                Logger::failure("companion", "save focus timers", StoragePaths::kFocusConfigPath, saved.error());
                return std::unexpected(companion::api::httpError(HTTP_CODE_INTERNAL_SERVER_ERROR,
                                                                 "focus_save_failed", saved.error().message()));
            }
            focusScreen_.setTimers(std::move(timers));
            return {};
        });
}
