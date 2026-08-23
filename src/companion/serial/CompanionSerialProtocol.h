#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace companion::serial {

    constexpr uint8_t kProtocolVersion = 1;
    constexpr size_t kChunkBytes = 4096;
    constexpr size_t kMaximumPayloadBytes = 64 * 1024;

    enum class FrameType : uint8_t {
        Request = 1,
        Data = 2,
        End = 3,
        Response = 4,
        Acknowledgement = 5,
        Error = 6,
        Ping = 7,
        Pong = 8,
        Close = 9,
    };

    struct Frame {
        FrameType type = FrameType::Error;
        uint32_t requestId = 0;
        uint32_t sequence = 0;
        std::vector<uint8_t> payload;
    };

    [[nodiscard]] uint32_t crc32(std::span<const uint8_t> bytes);
    [[nodiscard]] std::vector<uint8_t> encode(const Frame& frame);

    class Decoder {
    public:
        void append(std::span<const uint8_t> bytes);
        [[nodiscard]] std::vector<Frame> takeFrames();
        void clear();

    private:
        std::vector<uint8_t> buffer_;
    };

} // namespace companion::serial
