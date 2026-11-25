// Spec 036 (constitution 8.1.0, I): an end magnet read where the estimate says
// the roof can't be is not an arrival. Read early, the move goes on at the slow
// duty; the magnet is accepted once it has lasted and the estimate reaches
// roof.endPlausiblePercent or the reading is clearly at the magnet; else the
// EndTooEarly fault stops it with the position unknown. Moves from an unknown
// position, calibration legs and jogs keep their behaviour; a stop or a
// reversal part-way is unchanged. Alpaca never sees Open or Closed on an early
// reading.

#include <unity.h>

#include "../roof_sim/RoofSim.h"
#include "Config.h" // for the library finder: RoofLogic builds with ConfigModel
#include "DomeBridge.h"
#include "MotionProfile.h"
#include "RoofController.h"

using namespace SQM;
using namespace SQM::Roof;
using RoofTest::Bench;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    // The roof's own speed: the estimate uses the settings', the plant this.
    // A stray field reads `counts` between fromMm and toMm (the field run:
    // 2152 counts at about a third of the way, past a 2000 threshold).
    struct Stray
    {
        float fromMm = 0.0f;
        float toMm = 0.0f;
        uint16_t counts = 0;
    };

    struct Rig
    {
        Bench b;
        Stray stray;
        // Alpaca's view after every step: never Open or Closed while the
        // early reading is held (FR-006).
        bool sawOpenShutter = false;

        explicit Rig(const Settings &s = Settings(), float startMm = 0.0f)
            : b(s, startMm)
        {
        }

        Output step()
        {
            b.sim.drive(b.roof.output(), b.stepMs);
            b.nowMs += b.stepMs;
            Readings r = b.sim.readings(b.nowMs);
            if (b.sim.positionMm >= stray.fromMm && b.sim.positionMm <= stray.toMm)
                r.hallCounts = stray.counts;
            const Output out = b.roof.tick(r);
            const Alpaca::DomeStatus dome = RoofIo::domeStatus(b.roof.status(), false, true, RoofIo::WaitView{});
            if (dome.shutter == Alpaca::ShutterState::Open && b.sim.positionMm < b.sim.travelMm - 30.0f)
                sawOpenShutter = true;
            return out;
        }

        // Steps until the roof is neither opening nor closing.
        bool runToRest(uint32_t maxMs = 200000)
        {
            for (uint32_t t = 0; t < maxMs; t += b.stepMs)
            {
                step();
                const State s = b.state();
                if (s != State::Opening && s != State::Closing)
                    return true;
            }
            return false;
        }

        // Steps until the end check has judged an early reading (the move is held).
        uint32_t runToEarly(uint32_t maxMs = 100000)
        {
            for (uint32_t t = 0; t < maxMs; t += b.stepMs)
            {
                step();
                if (b.sim.positionMm >= stray.fromMm && b.roof.status().hall == HallLevel::High)
                    return t;
            }
            TEST_FAIL_MESSAGE("no early reading");
            return 0;
        }
    };

    uint8_t slowDuty(const Settings &s)
    {
        return toDuty(dutyFraction(s.slowDutyPercent));
    }

    const float TRAVEL = 1850.0f;
} // namespace

// The field run: the open threshold read at about a third of the way for a
// short stretch. Before: "open, 100 %". Now: held at the slow duty, the
// reading is lost, the fault stops the roof part-way, position unknown.
void test_a_short_stray_reading_part_way_is_the_early_end_fault(void)
{
    Rig r;
    r.stray = {0.32f * TRAVEL, 0.32f * TRAVEL + 60.0f, 3100};
    TEST_ASSERT_TRUE(r.b.command(Command::Open).accepted);
    r.runToEarly();
    for (int i = 0; i < 20; ++i)
        r.step(); // past the Hall filter's persistence
    TEST_ASSERT_TRUE(r.b.state() == State::Opening);
    TEST_ASSERT_EQUAL_UINT8(slowDuty(Settings()), r.b.roof.output().duty);
    TEST_ASSERT_TRUE(r.runToRest());
    const Status st = r.b.roof.status();
    TEST_ASSERT_TRUE(st.state == State::Error);
    TEST_ASSERT_TRUE(st.fault.kind == FaultKind::EndTooEarly);
    TEST_ASSERT_FLOAT_WITHIN(3.0f, 32.0f, st.fault.measured);
    TEST_ASSERT_EQUAL_FLOAT(80.0f, st.fault.limit);
    TEST_ASSERT_FALSE(st.positionKnown);
    TEST_ASSERT_TRUE(r.b.sim.positionMm < 0.40f * TRAVEL);
    TEST_ASSERT_FALSE(r.sawOpenShutter);
    TEST_ASSERT_TRUE(r.b.roof.endCheckReport().result == EndCheck::TooEarly);
    TEST_ASSERT_TRUE(Alpaca::ShutterState::Error == RoofIo::domeStatus(st, false, true, RoofIo::WaitView{}).shutter);
}

// A reading that stays at the threshold far from the end (a threshold taught
// too close to the reading between the magnets): held for EARLY_END_HOLD_MS at
// the slow duty, never plausible, the fault.
void test_a_lasting_early_reading_is_the_fault_after_the_hold(void)
{
    Rig r;
    r.stray = {0.30f * TRAVEL, TRAVEL, 3100};
    TEST_ASSERT_TRUE(r.b.command(Command::Open).accepted);
    r.runToEarly();
    const uint32_t heldFrom = r.b.nowMs;
    TEST_ASSERT_TRUE(r.runToRest());
    const Status st = r.b.roof.status();
    TEST_ASSERT_TRUE(st.fault.kind == FaultKind::EndTooEarly);
    TEST_ASSERT_FALSE(st.positionKnown);
    const uint32_t heldMs = r.b.nowMs - heldFrom;
    TEST_ASSERT_TRUE(heldMs >= EARLY_END_HOLD_MS);
    TEST_ASSERT_TRUE(heldMs <= EARLY_END_HOLD_MS + HALL_PERSIST_MS + 200);
    TEST_ASSERT_TRUE(r.b.sim.positionMm < 0.40f * TRAVEL);
    TEST_ASSERT_FALSE(r.sawOpenShutter);
    // A person clears it: still not open, and Open is refused until the roof
    // has been to the other end.
    TEST_ASSERT_TRUE(r.b.roof.clearFault(Origin::Person).accepted);
    r.step();
    TEST_ASSERT_TRUE(r.b.state() == State::Unknown);
    TEST_ASSERT_FALSE(r.b.roof.atEnd(Command::Open));
    TEST_ASSERT_TRUE(r.b.command(Command::Open).reason == Refusal::EndDoubted);
    TEST_ASSERT_TRUE(r.b.roof.check(Command::Open, Origin::Person).reason == Refusal::EndDoubted);
    r.stray = {};
    TEST_ASSERT_TRUE(r.b.command(Command::Close).accepted); // from unknown: slow, first magnet
    TEST_ASSERT_TRUE(r.runToRest());
    TEST_ASSERT_TRUE(r.b.state() == State::Closed);
    End end = End::Closed;
    TEST_ASSERT_FALSE(r.b.roof.endDoubted(end));
    TEST_ASSERT_TRUE(r.b.command(Command::Open).accepted);
    TEST_ASSERT_TRUE(r.runToRest());
    TEST_ASSERT_TRUE(r.b.state() == State::Open);
}

// An early reading near the plausible point that lasts to the end: held at the
// slow duty, accepted once the estimate reaches it.
void test_an_early_reading_is_accepted_at_the_plausible_point(void)
{
    Rig r;
    r.stray = {0.77f * TRAVEL, TRAVEL, 3100};
    TEST_ASSERT_TRUE(r.b.command(Command::Open).accepted);
    r.runToEarly();
    for (int i = 0; i < 20; ++i)
        r.step();
    TEST_ASSERT_TRUE(r.b.state() == State::Opening);
    TEST_ASSERT_EQUAL_UINT8(slowDuty(Settings()), r.b.roof.output().duty);
    TEST_ASSERT_TRUE(r.runToRest());
    TEST_ASSERT_TRUE(r.b.state() == State::Open);
    const EndCheckReport &rep = r.b.roof.endCheckReport();
    TEST_ASSERT_TRUE(rep.result == EndCheck::Late);
    TEST_ASSERT_TRUE(rep.progressPercent < 80.0f);
    TEST_ASSERT_TRUE(rep.heldMs >= EARLY_END_PERSIST_MS && rep.heldMs < EARLY_END_HOLD_MS);
    // Accepted where the estimate reached the plausible point, not before.
    TEST_ASSERT_TRUE(r.b.sim.positionMm >= 0.795f * TRAVEL);
    TEST_ASSERT_TRUE(r.b.sim.positionMm < 0.85f * TRAVEL);
}

// A roof faster than its speed setting reaches its real magnet with the
// estimate at about half way: the reading is clearly at the magnet (past the
// threshold by half the gap between the thresholds), so it is accepted.
void test_a_reading_clearly_at_the_magnet_is_accepted_early(void)
{
    Settings s;
    s.hallHighCounts = 2600; // clear at 2600 + (2600 - 1100) / 2 = 3350; the plant reads 3400
    s.speedMmPerSecond = 19.0f;
    Rig r(s);
    r.b.sim.speedMmPerSecond = 38.0f;
    TEST_ASSERT_TRUE(r.b.command(Command::Open).accepted);
    TEST_ASSERT_TRUE(r.runToRest());
    TEST_ASSERT_TRUE(r.b.state() == State::Open);
    const EndCheckReport &rep = r.b.roof.endCheckReport();
    TEST_ASSERT_TRUE(rep.result == EndCheck::Late);
    TEST_ASSERT_TRUE(rep.progressPercent < 80.0f);
    TEST_ASSERT_TRUE(rep.heldMs < EARLY_END_HOLD_MS);
}

// Without the clear level, the same early arrival at the real end is the fault:
// the roof stays put and a person checks it (the speed setting is wrong).
void test_a_weak_reading_at_a_misjudged_end_is_the_fault(void)
{
    Settings s;
    s.speedMmPerSecond = 19.0f;
    Rig r(s);
    r.b.sim.speedMmPerSecond = 38.0f;
    TEST_ASSERT_TRUE(r.b.command(Command::Open).accepted);
    TEST_ASSERT_TRUE(r.runToRest());
    TEST_ASSERT_TRUE(r.b.roof.status().fault.kind == FaultKind::EndTooEarly);
    TEST_ASSERT_FALSE(r.sawOpenShutter);
}

// A normal open and close: arrived at once, judged plausible, full duty
// profile untouched.
void test_a_normal_open_and_close_arrive_as_before(void)
{
    Rig r;
    TEST_ASSERT_TRUE(r.b.command(Command::Open).accepted);
    TEST_ASSERT_TRUE(r.runToRest());
    TEST_ASSERT_TRUE(r.b.state() == State::Open);
    TEST_ASSERT_TRUE(r.b.roof.endCheckReport().result == EndCheck::Plausible);
    TEST_ASSERT_TRUE(r.b.roof.endCheckReport().progressPercent >= 95.0f);
    TEST_ASSERT_TRUE(r.b.command(Command::Close).accepted);
    TEST_ASSERT_TRUE(r.runToRest());
    TEST_ASSERT_TRUE(r.b.state() == State::Closed);
    TEST_ASSERT_TRUE(r.b.roof.endCheckReport().result == EndCheck::Plausible);
    TEST_ASSERT_EQUAL_UINT32(2, r.b.roof.endCheckReport().ended);
}

// Reversal and stop part-way are unchanged; the reversed move's end is judged
// by the estimate from where it turned.
void test_a_reversal_and_a_stop_part_way_are_unchanged(void)
{
    Rig r;
    TEST_ASSERT_TRUE(r.b.command(Command::Open).accepted);
    for (int i = 0; i < 1000; ++i)
        r.step(); // 20 s: about half way
    TEST_ASSERT_TRUE(r.b.command(Command::Close).accepted);
    TEST_ASSERT_TRUE(r.runToRest());
    TEST_ASSERT_TRUE(r.b.state() == State::Closed);
    TEST_ASSERT_TRUE(r.b.roof.endCheckReport().result == EndCheck::Plausible);
    TEST_ASSERT_TRUE(r.b.command(Command::Open).accepted);
    for (int i = 0; i < 500; ++i)
        r.step();
    r.b.command(Command::Stop);
    r.b.steps(30);
    TEST_ASSERT_TRUE(r.b.state() == State::Stopped);
    TEST_ASSERT_TRUE(r.b.roof.status().positionKnown);
    TEST_ASSERT_TRUE(r.b.command(Command::Open).accepted);
    TEST_ASSERT_TRUE(r.runToRest());
    TEST_ASSERT_TRUE(r.b.state() == State::Open);
    TEST_ASSERT_TRUE(r.b.roof.endCheckReport().result == EndCheck::Plausible);
}

// A stop while an early reading is held: not Open, position unknown, and that
// end is doubted (Open refused) until the roof has been closed.
void test_a_stop_while_an_early_reading_is_held_leaves_the_position_unknown(void)
{
    Rig r;
    r.stray = {0.30f * TRAVEL, TRAVEL, 3100};
    TEST_ASSERT_TRUE(r.b.command(Command::Open).accepted);
    r.runToEarly();
    for (int i = 0; i < 20; ++i)
        r.step();
    r.b.command(Command::Stop);
    r.step();
    TEST_ASSERT_TRUE(r.b.state() == State::Unknown);
    TEST_ASSERT_FALSE(r.b.roof.status().positionKnown);
    TEST_ASSERT_FALSE(r.sawOpenShutter);
    End end = End::Closed;
    TEST_ASSERT_TRUE(r.b.roof.endDoubted(end));
    TEST_ASSERT_TRUE(end == End::Open);
    TEST_ASSERT_TRUE(r.b.command(Command::Open).reason == Refusal::EndDoubted);
    // A reversal while held starts from an unknown position too.
    Rig q;
    q.stray = r.stray;
    TEST_ASSERT_TRUE(q.b.command(Command::Open).accepted);
    q.runToEarly();
    for (int i = 0; i < 20; ++i)
        q.step();
    TEST_ASSERT_TRUE(q.b.command(Command::Close).accepted);
    TEST_ASSERT_TRUE(q.b.roof.status().fromUnknown);
    q.stray = {};
    TEST_ASSERT_TRUE(q.runToRest());
    TEST_ASSERT_TRUE(q.b.state() == State::Closed);
}

// A move from an unknown position has no estimate: the first magnet ends it
// as before, and the end check says it wasn't checked (for the log).
void test_a_move_from_an_unknown_position_is_unchanged(void)
{
    Rig r(Settings(), 900.0f);
    TEST_ASSERT_FALSE(r.b.roof.status().positionKnown);
    r.stray = {1000.0f, 1060.0f, 3100};
    TEST_ASSERT_TRUE(r.b.command(Command::Open).accepted);
    TEST_ASSERT_TRUE(r.runToRest());
    TEST_ASSERT_TRUE(r.b.state() == State::Open);
    TEST_ASSERT_TRUE(r.b.roof.endCheckReport().result == EndCheck::Unchecked);
}

// A calibration leg and a jog measure: they stop at the first magnet as before.
void test_calibration_legs_and_jogs_are_not_judged(void)
{
    Rig r;
    r.stray = {0.30f * TRAVEL, 0.30f * TRAVEL + 60.0f, 3100};
    CalibrationLeg leg;
    leg.target = End::Open;
    leg.dutyPercent = 40;
    leg.limitMs = 180000;
    TEST_ASSERT_TRUE(r.b.roof.calibrate(leg, Origin::Person).accepted);
    TEST_ASSERT_TRUE(r.runToRest());
    TEST_ASSERT_TRUE(r.b.roof.status().fault.kind == FaultKind::None);
    TEST_ASSERT_TRUE(r.b.roof.calibrationReport().arrived);
    TEST_ASSERT_EQUAL_UINT32(0, r.b.roof.endCheckReport().ended);
}

// New Hall settings (a re-teach) end the doubt.
void test_new_hall_settings_end_the_doubt(void)
{
    Rig r;
    r.stray = {0.30f * TRAVEL, TRAVEL, 3100};
    TEST_ASSERT_TRUE(r.b.command(Command::Open).accepted);
    TEST_ASSERT_TRUE(r.runToRest());
    TEST_ASSERT_TRUE(r.b.roof.clearFault(Origin::Person).accepted);
    End end = End::Closed;
    TEST_ASSERT_TRUE(r.b.roof.endDoubted(end));
    Settings same;
    TEST_ASSERT_TRUE(r.b.roof.applySettings(same).accepted);
    TEST_ASSERT_TRUE(r.b.roof.endDoubted(end)); // unrelated save: still doubted
    Settings taught;
    taught.hallHighCounts = 3200;
    TEST_ASSERT_TRUE(r.b.roof.applySettings(taught).accepted);
    TEST_ASSERT_FALSE(r.b.roof.endDoubted(end));
    for (int i = 0; i < 20; ++i)
        r.step();                                    // past the Hall filter's persistence
    TEST_ASSERT_TRUE(r.b.state() == State::Unknown); // 3100 < 3200: between
}

// A restored fault (after a restart) says no end: clearing it doubts the end
// the Hall reads then.
void test_a_restored_fault_doubts_the_end_read_when_cleared(void)
{
    Rig r(Settings(), TRAVEL);
    r.b.roof.raiseFault(Fault{FaultKind::EndTooEarly, 33.0f, 80.0f});
    r.step();
    TEST_ASSERT_TRUE(r.b.state() == State::Error);
    TEST_ASSERT_TRUE(r.b.roof.clearFault(Origin::Person).accepted);
    r.step();
    TEST_ASSERT_TRUE(r.b.state() == State::Unknown);
    TEST_ASSERT_TRUE(r.b.command(Command::Open).reason == Refusal::EndDoubted);
}

// The plausible point is a setting, inside its range.
void test_the_plausible_point_setting_and_its_range(void)
{
    Settings s;
    s.endPlausiblePercent = 49;
    TEST_ASSERT_FALSE(validSettings(s));
    s.endPlausiblePercent = 96;
    TEST_ASSERT_FALSE(validSettings(s));
    s.endPlausiblePercent = 50;
    TEST_ASSERT_TRUE(validSettings(s));
    RoofConfig c;
    TEST_ASSERT_EQUAL_UINT32(80, c.endPlausiblePercent);
    c.endPlausiblePercent = 60;
    TEST_ASSERT_EQUAL_UINT8(60, settingsFromConfig(c).endPlausiblePercent);
    // At 50 % the stray at 77 % is plausible at once.
    s.endPlausiblePercent = 50;
    Rig r(s);
    r.stray = {0.77f * TRAVEL, TRAVEL, 3100};
    TEST_ASSERT_TRUE(r.b.command(Command::Open).accepted);
    TEST_ASSERT_TRUE(r.runToRest());
    TEST_ASSERT_TRUE(r.b.roof.endCheckReport().result == EndCheck::Plausible);
}

// --- No end magnet where expected (spec 036, field: the open magnet shifted) ---

// The open magnet is missing: the move stops at 115 % of its expected time, at
// the slow duty, before the hard timeout; the position is unknown.
void test_no_end_magnet_where_expected_stops_before_the_timeout(void)
{
    Bench b;
    b.sim.openMagnetMissing = true;
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    uint8_t lastDuty = 0;
    for (uint32_t t = 0; t < 200000 && b.roof.status().fault.kind == FaultKind::None; t += b.stepMs)
    {
        lastDuty = b.roof.output().duty;
        b.step();
    }
    const Status st = b.roof.status();
    TEST_ASSERT_TRUE(st.fault.kind == FaultKind::NoEndMagnet);
    const uint32_t expected = expectedMoveMs(Settings(), TRAVEL);
    TEST_ASSERT_EQUAL_FLOAT(static_cast<float>(expected * 115 / 100), st.fault.limit);
    TEST_ASSERT_TRUE(st.fault.limit < static_cast<float>(moveTimeoutMs(Settings(), TRAVEL)));
    TEST_ASSERT_TRUE(st.fault.measured >= st.fault.limit && st.fault.measured < st.fault.limit + 20.0f);
    TEST_ASSERT_EQUAL_UINT8(slowDuty(Settings()), lastDuty); // creeping, not pushing at speed
    TEST_ASSERT_FALSE(st.positionKnown);
}

// A short move from near the end gets at least END_OVERRUN_MIN_MS beyond its expected time.
void test_a_short_move_gets_the_minimum_overrun(void)
{
    Bench b(Settings(), 0.0f);
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    b.steps(2300); // 46 s: near the open end
    b.command(Command::Stop);
    b.steps(30);
    TEST_ASSERT_TRUE(b.state() == State::Stopped);
    const float remaining = TRAVEL - b.roof.status().positionMm;
    b.sim.openMagnetMissing = true;
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    TEST_ASSERT_TRUE(b.runToRest());
    const Status st = b.roof.status();
    TEST_ASSERT_TRUE(st.fault.kind == FaultKind::NoEndMagnet);
    TEST_ASSERT_EQUAL_FLOAT(static_cast<float>(expectedMoveMs(Settings(), remaining) + END_OVERRUN_MIN_MS), st.fault.limit);
}

// A roof 10 % slower than its setting creeps to its magnet at the slow duty after
// the estimate arrives (spec 002); that takes it past 115 % of the expected time,
// so it needs a wider end overrun (or a calibration run).
void test_a_slower_roof_needs_a_wider_overrun(void)
{
    Bench tight;
    tight.sim.speedMmPerSecond = 34.2f;
    TEST_ASSERT_TRUE(tight.command(Command::Open).accepted);
    TEST_ASSERT_TRUE(tight.runToRest());
    TEST_ASSERT_TRUE(tight.roof.status().fault.kind == FaultKind::NoEndMagnet);
    Settings s;
    s.endOverrunPercent = 130;
    Bench b(s);
    b.sim.speedMmPerSecond = 34.2f;
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    TEST_ASSERT_TRUE(b.runToRest());
    TEST_ASSERT_TRUE(b.state() == State::Open);
}

// --- No movement detected (spec 036, field: a 25 % slow duty crawled with no sag) ---

namespace
{
    Settings watched(uint8_t slowDutyPercent)
    {
        Settings s;
        s.motionSagVolts = 0.2f;
        s.slowDutyPercent = slowDutyPercent;
        return s;
    }
} // namespace

// A healthy roof sags well past the threshold: a full open and close are unaffected.
void test_a_moving_roof_passes_the_movement_check(void)
{
    Bench b(watched(40));
    b.sim.sagVolts = 1.0f;
    TEST_ASSERT_TRUE(b.command(Command::Open).accepted);
    TEST_ASSERT_TRUE(b.runToRest());
    TEST_ASSERT_TRUE(b.state() == State::Open);
    TEST_ASSERT_TRUE(b.command(Command::Close).accepted);
    TEST_ASSERT_TRUE(b.runToRest());
    TEST_ASSERT_TRUE(b.state() == State::Closed);
}

// A move from an unknown position at a 25 % slow duty with almost no sag and a
// flat Hall reading stops after the surge and the check time.
void test_a_crawl_with_no_sag_is_no_movement(void)
{
    Bench b(watched(25), 900.0f);
    b.sim.sagVolts = 0.3f; // 0.075 V at 25 %
    TEST_ASSERT_TRUE(b.command(Command::Close).accepted);
    uint32_t drivenMs = 0;
    while (b.roof.status().fault.kind == FaultKind::None && drivenMs < 60000)
    {
        b.step();
        if (b.roof.status().driving)
            drivenMs += b.stepMs;
    }
    const Status st = b.roof.status();
    TEST_ASSERT_TRUE(st.fault.kind == FaultKind::NoMovement);
    TEST_ASSERT_UINT32_WITHIN(60, BLIND_SURGE_MS + 10000, drivenMs);
    TEST_ASSERT_TRUE(st.fault.measured < 0.2f);
    TEST_ASSERT_EQUAL_FLOAT(0.2f, st.fault.limit);
}

// A calibration leg (and a person's jog) gets twice the check time.
void test_a_calibration_leg_gets_twice_the_check_time(void)
{
    Bench b(watched(25));
    b.sim.sagVolts = 0.3f;
    CalibrationLeg leg;
    leg.target = End::Open;
    leg.dutyPercent = 25;
    leg.limitMs = 180000;
    TEST_ASSERT_TRUE(b.roof.calibrate(leg, Origin::Person).accepted);
    uint32_t drivenMs = 0;
    while (b.roof.status().fault.kind == FaultKind::None && drivenMs < 60000)
    {
        b.step();
        if (b.roof.status().driving)
            drivenMs += b.stepMs;
    }
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::NoMovement);
    // The window restarts as the Hall reading leaves the closed magnet.
    TEST_ASSERT_TRUE(drivenMs >= BLIND_SURGE_MS + 20000 && drivenMs < BLIND_SURGE_MS + 24000);
}

// Off (0 V): the crawl isn't judged.
void test_the_movement_check_can_be_off(void)
{
    Settings s = watched(25);
    s.motionSagVolts = 0.0f;
    TEST_ASSERT_TRUE(validSettings(s));
    Bench b(s, 900.0f);
    b.sim.sagVolts = 0.3f;
    TEST_ASSERT_TRUE(b.command(Command::Close).accepted);
    b.steps(1500); // 30 s
    TEST_ASSERT_TRUE(b.roof.status().fault.kind == FaultKind::None);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_short_stray_reading_part_way_is_the_early_end_fault);
    RUN_TEST(test_a_lasting_early_reading_is_the_fault_after_the_hold);
    RUN_TEST(test_an_early_reading_is_accepted_at_the_plausible_point);
    RUN_TEST(test_a_reading_clearly_at_the_magnet_is_accepted_early);
    RUN_TEST(test_a_weak_reading_at_a_misjudged_end_is_the_fault);
    RUN_TEST(test_a_normal_open_and_close_arrive_as_before);
    RUN_TEST(test_a_reversal_and_a_stop_part_way_are_unchanged);
    RUN_TEST(test_a_stop_while_an_early_reading_is_held_leaves_the_position_unknown);
    RUN_TEST(test_a_move_from_an_unknown_position_is_unchanged);
    RUN_TEST(test_calibration_legs_and_jogs_are_not_judged);
    RUN_TEST(test_new_hall_settings_end_the_doubt);
    RUN_TEST(test_a_restored_fault_doubts_the_end_read_when_cleared);
    RUN_TEST(test_the_plausible_point_setting_and_its_range);
    RUN_TEST(test_no_end_magnet_where_expected_stops_before_the_timeout);
    RUN_TEST(test_a_short_move_gets_the_minimum_overrun);
    RUN_TEST(test_a_slower_roof_needs_a_wider_overrun);
    RUN_TEST(test_a_moving_roof_passes_the_movement_check);
    RUN_TEST(test_a_crawl_with_no_sag_is_no_movement);
    RUN_TEST(test_a_calibration_leg_gets_twice_the_check_time);
    RUN_TEST(test_the_movement_check_can_be_off);
    return UNITY_END();
}
