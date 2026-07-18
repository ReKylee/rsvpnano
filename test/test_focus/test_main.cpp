#include <unity.h>

#include <string>
#include <utility>

#include "timer/FocusSession.h"
#include "timer/FocusTimers.h"

void setUp() {}

void tearDown() {}

namespace {

    focus::Timer shortTimer(uint8_t rounds = 2) {
        focus::Timer timer;
        timer.name = "Test";
        timer.focusMinutes = 1;
        timer.breakMinutes = 1;
        timer.rounds = rounds;
        return timer;
    }

    focus::Timer timer(std::string name, uint16_t focusMinutes, uint16_t breakMinutes, uint8_t rounds) {
        focus::Timer value;
        value.name = std::move(name);
        value.focusMinutes = focusMinutes;
        value.breakMinutes = breakMinutes;
        value.rounds = rounds;
        return value;
    }

}

void test_focus_config_round_trip_and_validation() {
    focus::Timers timers;
    timers.timers = {timer("Write \\\"code\\\"", 25, 5, 4), timer("Чтение", 45, 10, 2)};

    const std::string serialized = focus::encodeToml(timers);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, serialized.find("schemaVersion = 1"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, serialized.find("[[timers]]"));
    focus::Timers parsed;
    TEST_ASSERT_TRUE(focus::decodeToml(serialized, parsed));
    TEST_ASSERT_EQUAL(2, parsed.timers.size());
    TEST_ASSERT_EQUAL_STRING(timers.timers[0].name.c_str(), parsed.timers[0].name.c_str());
    TEST_ASSERT_EQUAL_STRING(timers.timers[1].name.c_str(), parsed.timers[1].name.c_str());
    TEST_ASSERT_EQUAL(45, parsed.timers[1].focusMinutes);

    const std::string clamped =
        "schemaVersion = 1\n[[timers]]\nname = \"Bounded\"\nfocusMinutes = 181\nbreakMinutes = 0\nrounds = 99\n";
    TEST_ASSERT_TRUE(focus::decodeToml(clamped, parsed));
    TEST_ASSERT_EQUAL(180, parsed.timers[0].focusMinutes);
    TEST_ASSERT_EQUAL(1, parsed.timers[0].breakMinutes);
    TEST_ASSERT_EQUAL(12, parsed.timers[0].rounds);
    TEST_ASSERT_FALSE(focus::decodeToml("schemaVersion = 2\n", parsed));
    TEST_ASSERT_FALSE(focus::decodeToml("schemaVersion = 1\nunknown = true\n", parsed));
    TEST_ASSERT_TRUE(focus::valid(timer("12345678901234", 25, 5, 4)));
    TEST_ASSERT_FALSE(focus::valid(timer("123456789012345", 25, 5, 4)));

    focus::Timers full;
    for (size_t index = 0; index < focus::kMaxTimers; ++index)
        full.timers.push_back(timer("Timer " + std::to_string(index), 25, 5, 4));
    TEST_ASSERT_TRUE(focus::decodeToml(focus::encodeToml(full), parsed));
    TEST_ASSERT_EQUAL(focus::kMaxTimers, parsed.timers.size());
    std::string tooMany = focus::encodeToml(full);
    tooMany += "\n[[timers]]\nname = \"Seventh\"\nfocusMinutes = 25\nbreakMinutes = 5\nrounds = 4\n";
    TEST_ASSERT_TRUE(focus::decodeToml(tooMany, parsed));
    TEST_ASSERT_EQUAL(focus::kMaxTimers, parsed.timers.size());
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
