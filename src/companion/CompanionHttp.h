#pragma once

#include <HTTPClient.h>

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include "companion/CompanionApiModels.h"

namespace companion::api {

    enum class ConnectionPolicy : uint8_t {
        KeepAlive,
        Close,
    };

    struct HttpError {
        t_http_codes status = HTTP_CODE_INTERNAL_SERVER_ERROR;
        ApiError error;
        ConnectionPolicy connection = ConnectionPolicy::KeepAlive;
    };

    template<typename T = void>
    using Result = std::expected<T, HttpError>;

    template<typename T>
    struct Located {
        std::string location;
        T value;
    };

    template<typename T>
    struct IsLocated : std::false_type {};

    template<typename T>
    struct IsLocated<Located<T>> : std::true_type {};

    template<typename T>
    inline constexpr bool isLocated = IsLocated<std::remove_cvref_t<T>>::value;

    [[nodiscard]] inline HttpError httpError(t_http_codes status, std::string code, std::string message,
                                             std::optional<std::string> field = std::nullopt,
                                             ConnectionPolicy connection = ConnectionPolicy::KeepAlive) {
        return {
            .status = status,
            .error =
                {
                    .code = std::move(code),
                    .message = std::move(message),
                    .field = std::move(field),
                },
            .connection = connection,
        };
    }

    [[nodiscard]] inline HttpError closeConnection(HttpError error) noexcept {
        error.connection = ConnectionPolicy::Close;
        return error;
    }

    [[nodiscard]] std::string httpStatusLine(t_http_codes status);

} // namespace companion::api
