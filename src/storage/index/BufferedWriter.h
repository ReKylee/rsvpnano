#pragma once

#include <Arduino.h>
#include <FS.h>
#include <expected>
#include <system_error>
#include <vector>

class BufferedWriter {
public:
    static constexpr size_t kDefaultCapacity = 4096;

    explicit BufferedWriter(File& file, size_t capacity = kDefaultCapacity);

    std::expected<void, std::error_code> write(const void* data, size_t len);
    std::expected<void, std::error_code> flush();
    std::expected<void, std::error_code> seek(uint32_t position);
    void discard();

private:
    File& file_;
    std::vector<uint8_t> buffer_;
    bool failed_ = false;
};
