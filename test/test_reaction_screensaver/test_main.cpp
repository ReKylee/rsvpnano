#include <unity.h>

#include "standby/ReactionScreensaver.h"

namespace {

void assertSameFrame(const standby::Frame& left, const standby::Frame& right) {
    TEST_ASSERT_EQUAL_UINT32(left.generation, right.generation);
    TEST_ASSERT_EQUAL(left.cells.wordCount, right.cells.wordCount);
    for (size_t i = 0; i < left.cells.wordCount; ++i) {
        TEST_ASSERT_EQUAL_HEX32(left.cells.words[i], right.cells.words[i]);
        TEST_ASSERT_EQUAL_HEX32(left.dimCells.words[i], right.dimCells.words[i]);
        TEST_ASSERT_EQUAL_HEX32(left.dirtyCells.words[i], right.dirtyCells.words[i]);
        TEST_ASSERT_EQUAL_HEX32(0, left.cells.words[i] & left.dimCells.words[i]);
    }
}

} // namespace

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
