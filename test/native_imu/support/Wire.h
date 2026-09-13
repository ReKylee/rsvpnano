#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

class TwoWire {
public:
    struct Transfer {
        uint8_t address;
        bool stop;
        std::vector<uint8_t> bytes;
    };
    std::array<uint8_t, 256> registers{};
    std::vector<Transfer> transfers;
    uint8_t respondingAddress = 0x6B;
    bool connected = true;
    bool shortRead = false;
    unsigned reads = 0;

    TwoWire() {
        registers[0x00] = 0x05;
        registers[0x4D] = 0x80;
    }
    void beginTransmission(uint8_t address) { address_ = address; bytes_.clear(); }
    size_t write(uint8_t byte) { bytes_.push_back(byte); return 1; }
    uint8_t endTransmission(bool stop) {
        transfers.push_back({address_, stop, bytes_});
        if (!connected || address_ != respondingAddress) return 2;
        if (!bytes_.empty()) cursor_ = bytes_[0];
        if (bytes_.size() == 2) registers[cursor_] = bytes_[1];
        return 0;
    }
    size_t requestFrom(uint8_t address, size_t count, bool) {
        ++reads;
        if (!connected || address != respondingAddress) return 0;
        return shortRead ? count - 1 : count;
    }
    int read() { return registers[cursor_++]; }
private:
    uint8_t address_ = 0;
    uint8_t cursor_ = 0;
    std::vector<uint8_t> bytes_;
};

#if defined(SOC_I2C_NUM)
extern TwoWire Wire;
#if SOC_I2C_NUM > 1
extern TwoWire Wire1;
#endif
#endif
