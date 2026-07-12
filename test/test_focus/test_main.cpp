#include <unity.h>

#include "timer/FocusSession.h"
#include "timer/FocusTimers.h"

void setUp() {}

void tearDown() {}

namespace {

    focus::Timer shortTimer(uint8_t rounds = 2) {
        return {.name = "Test", .focusMinutes = 1, .breakMinutes = 1, .rounds = rounds};
    }

}

void test_focus_config_round_trip_and_validation() {
    focus::Timers timers;
    timers.items[0] = {.name = "Write \\\"code\\\"", .focusMinutes = 25, .breakMinutes = 5, .rounds = 4};
    timers.items[1] = {.name = "Чтение", .focusMinutes = 45, .breakMinutes = 10, .rounds = 2};
    timers.count = 2;

    const std::string serialized = focus::serialize(timers);
    focus::Timers parsed;
    TEST_ASSERT_TRUE(focus::parse(serialized, parsed));
    TEST_ASSERT_EQUAL(2, parsed.count);
    TEST_ASSERT_EQUAL_STRING(timers.items[0].name.c_str(), parsed.items[0].name.c_str());
    TEST_ASSERT_EQUAL_STRING(timers.items[1].name.c_str(), parsed.items[1].name.c_str());
    TEST_ASSERT_EQUAL(45, parsed.items[1].focusMinutes);

    TEST_ASSERT_FALSE(focus::parse("version=1\n[[timer]]\nname=\"Bad\"\nfocus_minutes=25\nrounds=4\n", parsed));
    TEST_ASSERT_FALSE(focus::parse("version=1\n[[timer]]\nname=\"Bad\"\nname=\"Again\"\nfocus_minutes=25\nbreak_minutes=5\nrounds=4\n", parsed));
    TEST_ASSERT_FALSE(focus::parse("version=1\n[[timer]]\nname=\"Bad\"\nfocus_minutes=181\nbreak_minutes=5\nrounds=4\n", parsed));
    TEST_ASSERT_FALSE(focus::parse("version=1\n[[timer]]\nname=\"Bad\"\nfocus_minutes=25\nbreak_minutes=5\nrounds=4\ncolor=red\n", parsed));

    focus::Timers full;
    full.count = focus::kMaxTimers;
    for (size_t index = 0; index < full.count; ++index)
        full.items[index] = {.name = "Timer " + std::to_string(index), .focusMinutes = 25, .breakMinutes = 5, .rounds = 4};
    TEST_ASSERT_TRUE(focus::parse(focus::serialize(full), parsed));
    TEST_ASSERT_EQUAL(focus::kMaxTimers, parsed.count);
    std::string tooMany = focus::serialize(full);
    tooMany += "\n[[timer]]\nname=\"Ninth\"\nfocus_minutes=25\nbreak_minutes=5\nrounds=4\n";
    TEST_ASSERT_FALSE(focus::parse(tooMany, parsed));
}

void test_focus_session_flip_pause_and_completion() {
    focus::Session session;
    session.begin(shortTimer());
    session.update(1000, focus::Orientation::ShortA);
    TEST_ASSERT_EQUAL(focus::Phase::Focus, session.phase());

    session.update(11000, focus::Orientation::ShortB);
    TEST_ASSERT_EQUAL(focus::Phase::Focus, session.phase());
    session.update(21000, focus::Orientation::Flat);
    TEST_ASSERT_EQUAL(focus::Phase::PausedFocus, session.phase());
    TEST_ASSERT_EQUAL(40000, session.remainingMs(51000));
    session.update(51000, focus::Orientation::ShortA);
    TEST_ASSERT_EQUAL(focus::Phase::Focus, session.phase());

    session.update(91000, focus::Orientation::ShortA);
    TEST_ASSERT_EQUAL(focus::Phase::WaitingBreak, session.phase());
    TEST_ASSERT_EQUAL(1000, session.progressPermille(91000));
    TEST_ASSERT_TRUE(session.consumeCompletionCue());
    session.update(92000, focus::Orientation::ShortB);
    TEST_ASSERT_EQUAL(focus::Phase::Break, session.phase());
    TEST_ASSERT_EQUAL(0, session.progressPermille(92000));
    session.update(152000, focus::Orientation::ShortB);
    TEST_ASSERT_EQUAL(focus::Phase::WaitingFocus, session.phase());
    TEST_ASSERT_EQUAL(0, session.progressPermille(152000));
    session.update(153000, focus::Orientation::ShortA);
    TEST_ASSERT_EQUAL(focus::Phase::Focus, session.phase());
    session.update(213000, focus::Orientation::ShortA);
    TEST_ASSERT_EQUAL(focus::Phase::Complete, session.phase());
}

void test_focus_session_requires_post_expiry_flip_and_handles_wraparound() {
    focus::Session session;
    session.begin(shortTimer(2));
    constexpr uint32_t start = 0xFFFFFF00U;
    session.update(start, focus::Orientation::ShortA);
    session.update(start + 60000U, focus::Orientation::ShortB);
    TEST_ASSERT_EQUAL(focus::Phase::WaitingBreak, session.phase());

    session.update(start + 61000U, focus::Orientation::ShortB);
    TEST_ASSERT_EQUAL(focus::Phase::WaitingBreak, session.phase());
    session.update(start + 62000U, focus::Orientation::ShortA);
    session.update(start + 63000U, focus::Orientation::ShortB);
    TEST_ASSERT_EQUAL(focus::Phase::Break, session.phase());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_focus_config_round_trip_and_validation);
    RUN_TEST(test_focus_session_flip_pause_and_completion);
    RUN_TEST(test_focus_session_requires_post_expiry_flip_and_handles_wraparound);
    return UNITY_END();
}
