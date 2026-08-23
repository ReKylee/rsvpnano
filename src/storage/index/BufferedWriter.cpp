#include "storage/index/BufferedWriter.h"

#include <cstring>

BufferedWriter::BufferedWriter(File& file, size_t capacity) : file_(file) {
    buffer_.reserve(capacity);
}

BufferedWriter::~BufferedWriter() {
    flush();
}

std::expected<void, std::error_code> BufferedWriter::write(const void* data, size_t len) {
    if (failed_)
        return std::unexpected(std::make_error_code(std::errc::io_error));

    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    const size_t capacity = buffer_.capacity();
    if (capacity == 0) {
        if (file_.write(bytes, len) != len) {
            failed_ = true;
            return std::unexpected(std::make_error_code(std::errc::io_error));
        }
        return {};
    }

    if (buffer_.size() + len > capacity) {
        if (!flush()) {
            return std::unexpected(std::make_error_code(std::errc::io_error));
        }
        if (len >= capacity) {
            if (file_.write(bytes, len) != len) {
                failed_ = true;
                return std::unexpected(std::make_error_code(std::errc::io_error));
            }
            return {};
        }
    }

    const size_t offset = buffer_.size();
    buffer_.resize(offset + len);
    std::memcpy(buffer_.data() + offset, bytes, len);
    return {};
}

std::expected<void, std::error_code> BufferedWriter::flush() {
    if (failed_)
        return std::unexpected(std::make_error_code(std::errc::io_error));
    if (buffer_.empty())
        return {};
    const bool ok = file_.write(buffer_.data(), buffer_.size()) == buffer_.size();
    buffer_.clear();
    if (!ok) {
        failed_ = true;
        return std::unexpected(std::make_error_code(std::errc::io_error));
    }
    return {};
}

std::expected<void, std::error_code> BufferedWriter::seek(uint32_t position) {
    if (auto flushed = flush(); !flushed)
        return flushed;
    if (!file_.seek(position))
        return std::unexpected(std::make_error_code(std::errc::io_error));
    return {};
}

void BufferedWriter::discard() {
    buffer_.clear();
    failed_ = true;
}
