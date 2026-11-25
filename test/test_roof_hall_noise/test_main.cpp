// Hall noise from the motor (field report: a close faulted "wrong magnet"
// 620 ms in, and another mid-travel). The reading is replayed step by step
// into the controller with the roof's taught thresholds: open end low (1400),
// closed end high (1900), about 1800 in between, 1370 on the open magnet.
// Constitution I: a real wrong-direction move still faults, within a bound.

#include <string>
#include <unity.h>
#include <vector>

#include "Config.h"
#include "HallFilter.h"
#include "RoofController.h"
#include "MotionProfile.h"
#include "RoofSettings.h"

using namespace SQM;
using namespace SQM::Roof;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    constexpr uint32_t STEP_MS = 20;
    constexpr uint16_t OPEN_MAGNET = 1370;
    constexpr uint16_t BETWEEN = 1800;
    constexpr uint16_t CLOSED_MAGNET = 2300;

    Settings fieldSettings()
    {
        Settings s;
        s.hallHighEnd = End::Closed;
        s.hallHighCounts = 1900;
        s.hallLowCounts = 1400;
        return s;
    }

    struct Rig
    {
        Controller roof{fieldSettings()};
        uint32_t nowMs = 1000;

        Output step(uint16_t counts)
        {
            nowMs += STEP_MS;
            Readings r;
            r.nowMs = nowMs;
            r.hallCounts = counts;
            r.supplyVolts = 12.6f;
            return roof.tick(r);
        }

        void hold(uint16_t counts, uint32_t ms)
        {
            for (uint32_t t = 0; t < ms; t += STEP_MS)
                step(counts);
        }

        // At rest on the open magnet, past the start-up dead time.
        void restOpen() { hold(OPEN_MAGNET, 1000); }

        bool faulted() const { return roof.status().fault.kind != FaultKind::None; }
    };

    // A deterministic burst pattern: every `period` steps, `length` steps read `burst`.
    uint16_t withBursts(int i, uint16_t base, uint16_t burst, int period, int length)
    {
        return (i % period) < length ? burst : base;
    }
} // namespace

// The first field fault: leaving the open magnet the reading hovers just above
// the 1400 threshold, and start-up noise dips it back under for a few steps.
void test_hover_at_the_start_magnet_threshold_is_not_a_wrong_magnet(void)
{
    Rig rig;
    rig.restOpen();
    TEST_ASSERT_TRUE(rig.roof.status().state == State::Open);
    TEST_ASSERT_TRUE(rig.roof.command(Command::Close, Origin::Person).accepted);
    // 1.5 s around the threshold: 1450 with 3-step dips to 1395, then slowly out.
    for (int i = 0; i < 75; ++i)
        rig.step(withBursts(i, 1450, 1395, 6, 3));
    for (int i = 0; i < 50; ++i)
        rig.step(static_cast<uint16_t>(1450 + i * 7));
    rig.hold(BETWEEN, 2000);
    TEST_ASSERT_FALSE(rig.faulted());
    TEST_ASSERT_TRUE(rig.roof.status().state == State::Closing);
    TEST_ASSERT_TRUE(rig.roof.status().driving);
}

// The second: mid-travel EMI bursts of 400+ counts down to the open magnet's level.
void test_mid_travel_bursts_are_not_a_wrong_magnet(void)
{
    Rig rig;
    rig.restOpen();
    TEST_ASSERT_TRUE(rig.roof.command(Command::Close, Origin::Person).accepted);
    rig.hold(BETWEEN, 11000); // past the leave time: no start-magnet allowance
    // Repeated bursts of 1 to 5 steps (20-100 ms, up to half the time) to 1350,
    // the open magnet's reading.
    for (int length = 1; length <= 5; ++length)
    {
        for (int i = 0; i < 40; ++i)
            rig.step(withBursts(i, BETWEEN, 1350, 10, length));
        TEST_ASSERT_FALSE(rig.faulted());
    }
    // A single burst of 9 steps (180 ms): the median holds it for 7, under the persistence.
    for (int i = 0; i < 40; ++i)
        rig.step(withBursts(i, BETWEEN, 1350, 40, 9));
    TEST_ASSERT_FALSE(rig.faulted());
    // Isolated spikes up to the closed magnet's level don't arrive early either.
    for (int i = 0; i < 60; ++i)
        rig.step(withBursts(i, BETWEEN, CLOSED_MAGNET, 12, 4));
    TEST_ASSERT_FALSE(rig.faulted());
    TEST_ASSERT_TRUE(rig.roof.status().state == State::Closing);
}

// A real wrong magnet (the open one, read for longer than the persistence)
// still faults, within the bound: the median's two steps plus HALL_PERSIST_MS.
void test_a_held_wrong_magnet_faults_within_the_bound(void)
{
    Rig rig;
    rig.restOpen();
    TEST_ASSERT_TRUE(rig.roof.command(Command::Close, Origin::Person).accepted);
    rig.hold(BETWEEN, 12000); // past the leave time
    uint32_t ms = 0;
    while (!rig.faulted() && ms < 2000)
    {
        rig.step(1300);
        ms += STEP_MS;
    }
    TEST_ASSERT_TRUE(rig.roof.status().fault.kind == FaultKind::WrongMagnet);
    TEST_ASSERT_TRUE(ms <= HALL_PERSIST_MS + 3 * STEP_MS);
    TEST_ASSERT_TRUE(ms >= HALL_PERSIST_MS);
    TEST_ASSERT_TRUE(rig.roof.output().command == Driver::Command::Off);
}

// Back on the start magnet within the leave time: not a wrong magnet, the
// roof hasn't left it. Still there at the leave time: did not leave.
void test_back_on_the_start_magnet_within_the_leave_time(void)
{
    Rig rig;
    rig.restOpen();
    TEST_ASSERT_TRUE(rig.roof.command(Command::Close, Origin::Person).accepted);
    rig.hold(BETWEEN, 500); // left, then back within the re-entry grace
    rig.hold(OPEN_MAGNET, 1000);
    TEST_ASSERT_FALSE(rig.faulted());
    uint32_t ms = 0;
    while (!rig.faulted() && ms < 20000)
    {
        rig.step(OPEN_MAGNET);
        ms += STEP_MS;
    }
    TEST_ASSERT_TRUE(rig.roof.status().fault.kind == FaultKind::DidNotLeave);
    TEST_ASSERT_EQUAL_FLOAT(static_cast<float>(fieldSettings().leaveMagnetMs), rig.roof.status().fault.limit);
}

// Back on the start magnet longer than the re-entry grace after leaving it,
// though within the leave time: the roof came back, a wrong magnet.
void test_back_on_the_start_magnet_after_the_grace(void)
{
    Rig rig;
    rig.restOpen();
    TEST_ASSERT_TRUE(rig.roof.command(Command::Close, Origin::Person).accepted);
    rig.hold(BETWEEN, 3000);
    uint32_t ms = 0;
    while (!rig.faulted() && ms < 2000)
    {
        rig.step(OPEN_MAGNET);
        ms += STEP_MS;
    }
    TEST_ASSERT_TRUE(rig.roof.status().fault.kind == FaultKind::WrongMagnet);
    TEST_ASSERT_TRUE(ms <= HALL_PERSIST_MS + 3 * STEP_MS);
}

// Hovering in and out of the start magnet re-arms the grace each time, but
// never past the leave time: a roof still on it then faults didNotLeave.
void test_repeated_hover_is_bounded_by_the_leave_time(void)
{
    Rig rig;
    rig.restOpen();
    TEST_ASSERT_TRUE(rig.roof.command(Command::Close, Origin::Person).accepted);
    const uint32_t startMs = rig.nowMs;
    while (!rig.faulted() && rig.nowMs - startMs < 20000)
    {
        rig.hold(BETWEEN, 500);
        rig.hold(OPEN_MAGNET, 500);
    }
    const FaultKind kind = rig.roof.status().fault.kind;
    TEST_ASSERT_TRUE(kind == FaultKind::DidNotLeave || kind == FaultKind::WrongMagnet);
    TEST_ASSERT_TRUE(rig.nowMs - startMs <= fieldSettings().leaveMagnetMs + 1000);
}

// Back on the start magnet after the leave time: the roof runs backwards.
void test_back_on_the_start_magnet_after_the_leave_time(void)
{
    Rig rig;
    rig.restOpen();
    TEST_ASSERT_TRUE(rig.roof.command(Command::Close, Origin::Person).accepted);
    rig.hold(BETWEEN, 11000);
    rig.hold(OPEN_MAGNET, 400);
    TEST_ASSERT_TRUE(rig.roof.status().fault.kind == FaultKind::WrongMagnet);
    TEST_ASSERT_EQUAL_FLOAT(static_cast<float>(End::Open), rig.roof.status().fault.measured);
}

// Arriving still stops promptly: within the persistence and the median's delay.
// Spec 036: where the estimate allows it (most of the travel driven).
void test_arrival_within_the_bound(void)
{
    Rig rig;
    rig.restOpen();
    TEST_ASSERT_TRUE(rig.roof.command(Command::Close, Origin::Person).accepted);
    rig.hold(BETWEEN, expectedMoveMs(fieldSettings(), static_cast<float>(fieldSettings().travelMm)) - 1000);
    uint32_t ms = 0;
    while (rig.roof.status().state == State::Closing && ms < 2000)
    {
        rig.step(CLOSED_MAGNET);
        ms += STEP_MS;
    }
    TEST_ASSERT_TRUE(rig.roof.status().state == State::Closed);
    TEST_ASSERT_TRUE(ms <= HALL_PERSIST_MS + 3 * STEP_MS);
    TEST_ASSERT_TRUE(rig.roof.output().command == Driver::Command::Off);
}

// The Hall readings before a fault are kept for the log.
void test_the_readings_before_a_fault_are_kept(void)
{
    Rig rig;
    rig.restOpen();
    TEST_ASSERT_TRUE(rig.roof.command(Command::Close, Origin::Person).accepted);
    rig.hold(BETWEEN, 11000);
    rig.hold(1300, 400);
    TEST_ASSERT_TRUE(rig.roof.status().fault.kind == FaultKind::WrongMagnet);
    const HallTrail &trail = rig.roof.faultHall();
    TEST_ASSERT_EQUAL_UINT8(HALL_TRAIL_READINGS, trail.count);
    TEST_ASSERT_EQUAL_UINT16(1300, trail.counts[HALL_TRAIL_READINGS - 1]);
    // Later readings don't change it.
    rig.hold(BETWEEN, 200);
    TEST_ASSERT_EQUAL_UINT16(1300, rig.roof.faultHall().counts[HALL_TRAIL_READINGS - 1]);
}

// Thresholds under the 400-count low rail saved by older firmware: the stored
// settings still load (the rest is kept), the roof can't move on them, and a
// save needs them at 500 or more.
void test_old_low_hall_thresholds_load_but_cannot_be_saved()
{
    Config cfg = Config::createDefault();
    std::string error;
    TEST_ASSERT_TRUE(Config::applyJson("{\"roof\":{\"hallHighCounts\":1200,\"hallLowCounts\":300}}", cfg, false, &error));
    TEST_ASSERT_EQUAL_UINT32(300, cfg.roof.hallLowCounts);
    TEST_ASSERT_FALSE(cfg.validate(&error));
    TEST_ASSERT_EQUAL_STRING("Roof: Hall low threshold must be between 500 and 3995 counts", error.c_str());
    TEST_ASSERT_FALSE(validSettings(settingsFromConfig(cfg.roof)));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_hover_at_the_start_magnet_threshold_is_not_a_wrong_magnet);
    RUN_TEST(test_mid_travel_bursts_are_not_a_wrong_magnet);
    RUN_TEST(test_a_held_wrong_magnet_faults_within_the_bound);
    RUN_TEST(test_back_on_the_start_magnet_within_the_leave_time);
    RUN_TEST(test_back_on_the_start_magnet_after_the_grace);
    RUN_TEST(test_back_on_the_start_magnet_after_the_leave_time);
    RUN_TEST(test_repeated_hover_is_bounded_by_the_leave_time);
    RUN_TEST(test_arrival_within_the_bound);
    RUN_TEST(test_the_readings_before_a_fault_are_kept);
    RUN_TEST(test_old_low_hall_thresholds_load_but_cannot_be_saved);
    return UNITY_END();
}
