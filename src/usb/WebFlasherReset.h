#pragma once

#include <array>
#include <cstdint>

namespace usb {

    class WebFlasherResetSequence {
    public:
        bool update(bool dtr, bool rts) {
            const uint8_t signals = static_cast<uint8_t>((dtr ? 2 : 0) | (rts ? 1 : 0));
            if (signals == kSequence[step_]) {
                if (++step_ == kSequence.size()) {
                    step_ = 0;
                    return true;
                }
            } else {
                step_ = signals == kSequence.front() ? 1 : 0;
            }
            return false;
        }

    private:
        static constexpr std::array<uint8_t, 4> kSequence{2, 3, 1, 0};
        uint8_t step_ = 0;
    };

    void enableWebFlasherReset();

} // namespace usb
