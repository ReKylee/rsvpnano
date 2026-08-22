#include <unity.h>

#include "standby/ReactionScreensaver.h"

namespace {

    void assertSameFrame(const standby::Frame& left, const standby::Frame& right) {
        TEST_ASSERT_EQUAL_UINT32(left.generation, right.generation);
        TEST_ASSERT_EQUAL(left.cells.size(), right.cells.size());
        for (size_t i = 0; i < left.cells.size(); ++i) {
            TEST_ASSERT_EQUAL_HEX32(left.cells[i], right.cells[i]);
            TEST_ASSERT_EQUAL_HEX32(left.dimCells[i], right.dimCells[i]);
            TEST_ASSERT_EQUAL_HEX32(left.dirtyCells[i], right.dirtyCells[i]);
            TEST_ASSERT_EQUAL_HEX32(0, left.cells[i] & left.dimCells[i]);
        }
    }

} // namespace

void setUp() {}
void tearDown() {}

void test_reaction_is_deterministic_and_keeps_visual_states_disjoint() {
    standby::ReactionScreensaver left;
    standby::ReactionScreensaver right;
    left.reset(32, 16);
    right.reset(32, 16);
    left.seed(12345);
    right.seed(12345);

    TEST_ASSERT_TRUE(left.frame().fullRedraw);
    assertSameFrame(left.frame(), right.frame());

    left.step();
    right.step();
    const standby::Frame frame = left.frame();
    TEST_ASSERT_FALSE(frame.fullRedraw);
    TEST_ASSERT_EQUAL_UINT32(1, frame.generation);
    TEST_ASSERT_TRUE(standby::anyCellAlive(frame.dirtyCells));
    assertSameFrame(frame, right.frame());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_reaction_is_deterministic_and_keeps_visual_states_disjoint);
    return UNITY_END();
}
