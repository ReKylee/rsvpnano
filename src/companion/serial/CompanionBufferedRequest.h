#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>

#include <esp_http_server.h>

namespace companion {

    using BufferedRead = int (*)(void* context, std::span<uint8_t> destination);

    struct BufferedRequest {
        std::string path;
        std::map<std::string, std::string, std::less<>> query;
        BufferedRead read = nullptr;
        void* readContext = nullptr;
        std::string location;
    };

    [[nodiscard]] inline BufferedRequest* bufferedRequest(httpd_req_t& request) {
        return request.handle == nullptr ? static_cast<BufferedRequest*>(request.user_ctx) : nullptr;
    }

    [[nodiscard]] inline const BufferedRequest* bufferedRequest(const httpd_req_t& request) {
        return request.handle == nullptr ? static_cast<const BufferedRequest*>(request.user_ctx) : nullptr;
    }

} // namespace companion
