#include "companion/http/CompanionUpload.h"
#include "companion/serial/CompanionBufferedRequest.h"

#include <esp_log.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>

#include "logging/Logger.h"

namespace {

    namespace api = companion::api;
    constexpr size_t kUploadChunkBytes = 4096;

    [[nodiscard]] api::HttpError interruptedUpload(t_http_codes status, std::string code,
                                                   std::string message) {
        return api::httpError(status, std::move(code), std::move(message), "file",
                              api::ConnectionPolicy::Close);
    }

} // namespace

namespace companion {

    TemporaryUpload::TemporaryUpload(fs::FS& filesystem, std::string temporaryPath) :
            filesystem_(&filesystem), path_(std::move(temporaryPath)) {}

    TemporaryUpload::TemporaryUpload(TemporaryUpload&& other) noexcept :
            filesystem_(std::exchange(other.filesystem_, nullptr)), path_(std::move(other.path_)) {
        other.path_.clear();
    }

    TemporaryUpload& TemporaryUpload::operator=(TemporaryUpload&& other) noexcept {
        if (this == &other)
            return *this;

        cleanup();
        filesystem_ = std::exchange(other.filesystem_, nullptr);
        path_ = std::move(other.path_);
        other.path_.clear();
        return *this;
    }

    TemporaryUpload::~TemporaryUpload() {
        cleanup();
    }

    const std::string& TemporaryUpload::path() const noexcept {
        return path_;
    }

    api::Result<TemporaryUpload> TemporaryUpload::receive(httpd_req_t& request, fs::FS& filesystem,
                                                           std::string temporaryPath, size_t maximumBytes,
                                                           std::string_view label, UploadChunkConsumer consume,
                                                           void* consumeContext) {
        if (request.content_len == 0) {
            return std::unexpected(api::httpError(HTTP_CODE_BAD_REQUEST, "missing_upload",
                                                  std::string{label} + " file is required", "file"));
        }

        if (request.content_len > maximumBytes) {
            return std::unexpected(api::httpError(HTTP_CODE_PAYLOAD_TOO_LARGE, "payload_too_large",
                                                  std::string{label} + " is too large", "file",
                                                  api::ConnectionPolicy::Close));
        }

        if (filesystem.exists(temporaryPath.c_str())
            && !filesystem.remove(temporaryPath.c_str())) {
            return std::unexpected(api::httpError(
                HTTP_CODE_INTERNAL_SERVER_ERROR, "storage_error",
                "Could not remove a previous temporary upload", "file",
                api::ConnectionPolicy::Close));
        }

        TemporaryUpload upload{filesystem, std::move(temporaryPath)};
        File file = filesystem.open(upload.path().c_str(), FILE_WRITE);
        if (!file) {
            return std::unexpected(api::httpError(HTTP_CODE_INTERNAL_SERVER_ERROR, "storage_error",
                                                  "Could not create temporary upload file", "file",
                                                  api::ConnectionPolicy::Close));
        }

        std::array<std::byte, kUploadChunkBytes> buffer{};
        size_t remaining = request.content_len;
        while (remaining > 0) {
            const size_t requested = std::min(remaining, buffer.size());
            const int received = companion::bufferedRequest(request) != nullptr
                                   ? companion::bufferedRequest(request)->read(
                                         companion::bufferedRequest(request)->readContext,
                                         std::span{reinterpret_cast<uint8_t*>(buffer.data()), requested})
                                   : httpd_req_recv(&request, reinterpret_cast<char*>(buffer.data()), requested);
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                return std::unexpected(interruptedUpload(
                    HTTP_CODE_REQUEST_TIMEOUT, "request_timeout",
                    std::string{label} + " upload timed out"));
            }
            if (received <= 0) {
                return std::unexpected(interruptedUpload(
                    HTTP_CODE_BAD_REQUEST, "upload_interrupted",
                    std::string{label} + " upload was interrupted"));
            }

            const size_t receivedBytes = static_cast<size_t>(received);
            const size_t written =
                file.write(reinterpret_cast<const uint8_t*>(buffer.data()), receivedBytes);
            if (written != receivedBytes) {
                return std::unexpected(api::httpError(HTTP_CODE_INTERNAL_SERVER_ERROR, "storage_error",
                                                      std::string{label} + " upload could not be written", "file",
                                                      api::ConnectionPolicy::Close));
            }
            if (consume != nullptr) {
                auto consumed = consume(consumeContext,
                                        std::span{reinterpret_cast<const uint8_t*>(buffer.data()), receivedBytes});
                if (!consumed) {
                    Logger::failure("companion", "process upload", upload.path().c_str(), consumed.error());
                    return std::unexpected(api::httpError(HTTP_CODE_INTERNAL_SERVER_ERROR, "index_error",
                                                          std::string{label} + " could not be indexed", "file",
                                                          api::ConnectionPolicy::Close));
                }
            }
            remaining -= receivedBytes;
        }

        file.flush();
        file.close();
        return upload;
    }

    void TemporaryUpload::cleanup() noexcept {
        if (filesystem_ != nullptr && !path_.empty() && filesystem_->exists(path_.c_str())
            && !filesystem_->remove(path_.c_str())) {
            ESP_LOGW("companion", "could not remove temporary upload %s", path_.c_str());
        }
        path_.clear();
        filesystem_ = nullptr;
    }

} // namespace companion
