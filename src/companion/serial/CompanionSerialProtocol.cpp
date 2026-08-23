#include "companion/serial/CompanionSerialProtocol.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace {

    constexpr std::array<uint8_t, 4> kMagic{'R', 'S', 'V', '2'};
    constexpr size_t kHeaderBytes = 18;
    constexpr size_t kTrailerBytes = 4;

    void writeU32(std::vector<uint8_t>& output, size_t offset, uint32_t value) {
        for (size_t byte = 0; byte < 4; ++byte)
            output[offset + byte] = static_cast<uint8_t>(value >> (byte * 8));
    }

    [[nodiscard]] uint32_t readU32(std::span<const uint8_t> input, size_t offset) {
        uint32_t value = 0;
        for (size_t byte = 0; byte < 4; ++byte)
            value |= static_cast<uint32_t>(input[offset + byte]) << (byte * 8);
        return value;
    }

    [[nodiscard]] bool knownType(uint8_t value) {
        return value >= static_cast<uint8_t>(companion::serial::FrameType::Request)
            && value <= static_cast<uint8_t>(companion::serial::FrameType::Close);
    }

} // namespace

namespace companion::serial {

    uint32_t crc32(std::span<const uint8_t> bytes) {
        uint32_t crc = 0xFFFFFFFFU;
        for (const uint8_t value: bytes) {
            crc ^= value;
            for (uint8_t bit = 0; bit < 8; ++bit)
                crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
        return ~crc;
    }

    std::vector<uint8_t> encode(const Frame& frame) {
        if (frame.payload.size() > kMaximumPayloadBytes)
            return {};

        std::vector<uint8_t> output(kHeaderBytes + frame.payload.size() + kTrailerBytes);
        std::ranges::copy(kMagic, output.begin());
        output[4] = kProtocolVersion;
        output[5] = static_cast<uint8_t>(frame.type);
        writeU32(output, 6, frame.requestId);
        writeU32(output, 10, frame.sequence);
        writeU32(output, 14, static_cast<uint32_t>(frame.payload.size()));
        std::ranges::copy(frame.payload, output.begin() + kHeaderBytes);
        writeU32(output, kHeaderBytes + frame.payload.size(),
                 crc32(std::span{output}.subspan(4, kHeaderBytes + frame.payload.size() - 4)));
        return output;
    }

    void Decoder::append(std::span<const uint8_t> bytes) {
        buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
    }

    std::vector<Frame> Decoder::takeFrames() {
        std::vector<Frame> frames;
        while (true) {
            const auto magic = std::search(buffer_.begin(), buffer_.end(), kMagic.begin(), kMagic.end());
            if (magic == buffer_.end()) {
                if (buffer_.size() > kMagic.size() - 1)
                    buffer_.erase(buffer_.begin(), buffer_.end() - static_cast<ptrdiff_t>(kMagic.size() - 1));
                break;
            }
            buffer_.erase(buffer_.begin(), magic);
            if (buffer_.size() < kHeaderBytes + kTrailerBytes)
                break;

            const uint32_t payloadBytes = readU32(buffer_, 14);
            if (buffer_[4] != kProtocolVersion || !knownType(buffer_[5]) || payloadBytes > kMaximumPayloadBytes) {
                buffer_.erase(buffer_.begin());
                continue;
            }

            const size_t frameBytes = kHeaderBytes + payloadBytes + kTrailerBytes;
            if (buffer_.size() < frameBytes)
                break;

            const uint32_t expected = readU32(buffer_, kHeaderBytes + payloadBytes);
            const uint32_t actual = crc32(std::span{buffer_}.subspan(4, kHeaderBytes + payloadBytes - 4));
            if (expected != actual) {
                buffer_.erase(buffer_.begin());
                continue;
            }

            Frame frame{
                .type = static_cast<FrameType>(buffer_[5]),
                .requestId = readU32(buffer_, 6),
                .sequence = readU32(buffer_, 10),
            };
            frame.payload.assign(buffer_.begin() + kHeaderBytes,
                                 buffer_.begin() + kHeaderBytes + payloadBytes);
            frames.push_back(std::move(frame));
            buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<ptrdiff_t>(frameBytes));
        }
        return frames;
    }

    void Decoder::clear() {
        buffer_.clear();
    }

} // namespace companion::serial
