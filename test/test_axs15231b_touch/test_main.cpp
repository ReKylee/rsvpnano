#include <unity.h>

#include <array>
#include <cstdint>

#include "drivers/touch/axs15231b_touch/axs15231b_touch.h"

namespace {

    constexpr uint16_t kPanelWidth = 172;
    constexpr uint16_t kPanelHeight = 640;

    BoardDrivers::Touch::Sample decode(const std::array<uint8_t, Axs15231bTouch::kPacketLength>& packet) {
        BoardDrivers::Touch::Sample sample = {.touched = true};
        TEST_ASSERT_TRUE(Axs15231bTouch::decodePacket(packet.data(), packet.size(), kPanelWidth, kPanelHeight, sample));
        return sample;
    }

} // namespace

void setUp() {}
void tearDown() {}

void test_decodes_zero_gesture_touch() {
    const auto sample = decode({0x00, 0x01, 0x00, 0x64, 0x00, 0x32, 0x00, 0x00});
    TEST_ASSERT_TRUE(sample.touched);
    TEST_ASSERT_EQUAL_UINT16(50, sample.physicalX);
    TEST_ASSERT_EQUAL_UINT16(539, sample.physicalY);
}

void test_decodes_zero_point_release() {
    TEST_ASSERT_FALSE(decode({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}).touched);
}

void test_rejects_02_filler_frame() {
    std::array<uint8_t, Axs15231bTouch::kPacketLength> packet;
    packet.fill(0x02);
    TEST_ASSERT_FALSE(decode(packet).touched);
}

void test_rejects_ff_filler_frame() {
    std::array<uint8_t, Axs15231bTouch::kPacketLength> packet;
    packet.fill(0xFF);
    TEST_ASSERT_FALSE(decode(packet).touched);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_decodes_zero_gesture_touch);
    RUN_TEST(test_decodes_zero_point_release);
    RUN_TEST(test_rejects_02_filler_frame);
    RUN_TEST(test_rejects_ff_filler_frame);
    return UNITY_END();
}
