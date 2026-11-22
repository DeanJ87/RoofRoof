// Spec 021: a move the park interlock watches rides through a missed N.I.N.A.
// reply on the last good, parked reading while it is fresh (NINA_FRESH_MS),
// and stops on any evidence of "not parked" (constitution II). The pure parts:
// the memory (ParkRule rememberNina), the hold (closeHold) and, with the gates
// (RoofGates closeMayContinue / interlockLost), the timeline of a close.

#include <string>
#include <unity.h>

#include "NinaApi.h"
#include "ParkRule.h"
#include "RoofGates.h"

using namespace SQM;
using namespace SQM::Safety;
using Nina::MountReading;
using Nina::ReadingStatus;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    constexpr uint64_t T0 = 500000; // the last good reading's stamp
    constexpr uint32_t GEN = 3;

    MountReading good(bool atPark = true, bool slewing = false, bool connected = true, uint64_t at = T0)
    {
        MountReading m;
        m.status = ReadingStatus::Good;
        m.connected = connected;
        m.atPark = atPark;
        m.slewing = slewing;
        m.takenAtMs = at;
        return m;
    }

    ParkInputs inputsAt(const MountReading &nina, uint64_t nowMs, uint8_t fitted = 0)
    {
        ParkInputs in;
        in.nina = nina;
        in.fittedSwitches = fitted;
        in.switch1 = SwitchReading::Closed;
        in.switch2 = SwitchReading::Closed;
        in.nowMs = nowMs;
        return in;
    }

    // One loop evaluation: remember, evaluate, hold.
    CloseHold evaluate(NinaMemory &memory, const ParkInputs &in, uint32_t generation = GEN)
    {
        rememberNina(memory, in.nina, generation);
        return closeHold(in, evaluatePark(in), memory);
    }

    MountReading failed(ReadingStatus status, uint64_t at)
    {
        return Nina::failedReading(status, "read Timeout", at);
    }

    RoofIo::GateInputs gatesFrom(const ParkInputs &in, const CloseHold &hold, uint64_t nowMs)
    {
        RoofIo::GateInputs g;
        g.weather = Verdict::Safe;
        g.parked = hold.parked;
        g.ninaGrace = hold.riding;
        g.parkFreshUntilMs = hold.freshUntilMs;
        g.nowMs = nowMs;
        g.fittedSwitches = in.fittedSwitches;
        g.switch1 = RoofIo::Contact::Closed;
        g.switch2 = RoofIo::Contact::Closed;
        return g;
    }

    Roof::Status closing()
    {
        Roof::Status s;
        s.state = Roof::State::Closing;
        return s;
    }
    RoofIo::GateInputs parkedGates(uint8_t fitted)
    {
        RoofIo::GateInputs in;
        in.weather = Verdict::Safe;
        in.parked = true;
        in.fittedSwitches = fitted;
        in.switch1 = RoofIo::Contact::Closed;
        in.switch2 = RoofIo::Contact::Closed;
        return in;
    }

    Roof::Status inState(Roof::State state)
    {
        Roof::Status s;
        s.state = state;
        return s;
    }
} // namespace

// --- The memory -------------------------------------------------------------

void test_memory_keeps_the_latest_good_reading_across_failures(void)
{
    NinaMemory m;
    rememberNina(m, good(), GEN);
    TEST_ASSERT_TRUE(m.known);
    for (ReadingStatus s : {ReadingStatus::Unreachable, ReadingStatus::Unreadable, ReadingStatus::ApiError})
    {
        rememberNina(m, failed(s, T0 + 2000), GEN);
        TEST_ASSERT_TRUE(m.known);
        TEST_ASSERT_EQUAL_UINT64(T0, m.lastGood.takenAtMs);
    }
    rememberNina(m, good(false, false, true, T0 + 4000), GEN);
    TEST_ASSERT_FALSE(m.lastGood.atPark); // a good reading always replaces it
    TEST_ASSERT_EQUAL_UINT64(T0 + 4000, m.lastGood.takenAtMs);
}

void test_memory_forgets_on_a_new_address_or_no_reading(void)
{
    NinaMemory m;
    rememberNina(m, good(), GEN);
    rememberNina(m, failed(ReadingStatus::Unreachable, T0 + 1000), GEN + 1);
    TEST_ASSERT_FALSE(m.known);
    for (ReadingStatus s : {ReadingStatus::Waiting, ReadingStatus::NotConfigured})
    {
        rememberNina(m, good(), GEN);
        rememberNina(m, failed(s, T0 + 1000), GEN);
        TEST_ASSERT_FALSE(m.known);
    }
}

// --- The hold ---------------------------------------------------------------

void test_parked_holds_until_the_reading_is_stale(void)
{
    NinaMemory m;
    const CloseHold h = evaluate(m, inputsAt(good(), T0 + 1500));
    TEST_ASSERT_TRUE(h.parked);
    TEST_ASSERT_FALSE(h.riding);
    TEST_ASSERT_EQUAL_UINT64(T0 + NINA_FRESH_MS, h.freshUntilMs);
    TEST_ASSERT_EQUAL_UINT32(1500, h.ageMs);
    TEST_ASSERT_EQUAL_STRING("", describe(h, NINA_FRESH_MS).c_str());
}

void test_each_read_failure_rides_on_the_parked_reading(void)
{
    for (ReadingStatus s : {ReadingStatus::Unreachable, ReadingStatus::Unreadable, ReadingStatus::ApiError})
    {
        NinaMemory m;
        evaluate(m, inputsAt(good(), T0 + 500));
        const CloseHold h = evaluate(m, inputsAt(failed(s, T0 + 3200), T0 + 3200));
        TEST_ASSERT_FALSE(h.parked);
        TEST_ASSERT_TRUE(h.riding);
        TEST_ASSERT_EQUAL_UINT64(T0 + NINA_FRESH_MS, h.freshUntilMs);
        TEST_ASSERT_EQUAL_STRING("riding on a parked reading 3.2 s old (limit 10 s)", describe(h, NINA_FRESH_MS).c_str());
    }
}

void test_ride_through_boundary_at_the_freshness_limit(void)
{
    NinaMemory m;
    evaluate(m, inputsAt(good(), T0));
    const uint64_t last = T0 + NINA_FRESH_MS; // the last step before stale: 10 000 ms old
    TEST_ASSERT_TRUE(evaluate(m, inputsAt(failed(ReadingStatus::Unreachable, last), last)).riding);
    const uint64_t stale = last + 1;
    const CloseHold h = evaluate(m, inputsAt(failed(ReadingStatus::Unreachable, stale), stale));
    TEST_ASSERT_FALSE(h.riding);
    TEST_ASSERT_FALSE(h.parked);
    TEST_ASSERT_EQUAL_UINT64(0, h.freshUntilMs);
}

void test_failure_then_not_parked_evidence_stops_the_ride(void)
{
    const MountReading notParked[] = {good(false), good(true, true), good(true, false, false)};
    for (const MountReading &evidence : notParked)
    {
        NinaMemory m;
        evaluate(m, inputsAt(good(), T0));
        TEST_ASSERT_TRUE(evaluate(m, inputsAt(failed(ReadingStatus::Unreachable, T0 + 2000), T0 + 2000)).riding);
        MountReading now = evidence;
        now.takenAtMs = T0 + 4000;
        const CloseHold h = evaluate(m, inputsAt(now, T0 + 4000));
        TEST_ASSERT_FALSE(h.parked);
        TEST_ASSERT_FALSE(h.riding);
        // And a failure after it rides on nothing: the not-parked reading is remembered.
        TEST_ASSERT_FALSE(evaluate(m, inputsAt(failed(ReadingStatus::Unreachable, T0 + 6000), T0 + 6000)).riding);
    }
}

void test_no_ride_with_a_switch_open_or_unreadable(void)
{
    for (SwitchReading sw : {SwitchReading::Open, SwitchReading::Unreadable})
    {
        NinaMemory m;
        evaluate(m, inputsAt(good(), T0, 1));
        ParkInputs in = inputsAt(failed(ReadingStatus::Unreachable, T0 + 2000), T0 + 2000, 1);
        in.switch1 = sw;
        const CloseHold h = evaluate(m, in);
        TEST_ASSERT_FALSE(h.riding);
        TEST_ASSERT_EQUAL_UINT64(0, h.freshUntilMs);
    }
    // The second fitted switch too.
    NinaMemory m;
    evaluate(m, inputsAt(good(), T0, 2));
    ParkInputs in = inputsAt(failed(ReadingStatus::Unreachable, T0 + 2000), T0 + 2000, 2);
    in.switch2 = SwitchReading::Open;
    TEST_ASSERT_FALSE(evaluate(m, in).riding);
}

void test_no_ride_on_a_remembered_reading_that_was_not_parked(void)
{
    NinaMemory m;
    evaluate(m, inputsAt(good(false), T0, 1));
    TEST_ASSERT_FALSE(evaluate(m, inputsAt(failed(ReadingStatus::ApiError, T0 + 2000), T0 + 2000, 1)).riding);
    // Remembered not at park while the switches say parked: they disagree.
    ParkInputs in = inputsAt(failed(ReadingStatus::Unreachable, T0 + 2000), T0 + 2000, 1);
    TEST_ASSERT_FALSE(closeHold(in, evaluatePark(in), m).riding);
}

void test_no_ride_without_a_memory_or_for_other_reasons(void)
{
    NinaMemory empty;
    ParkInputs in = inputsAt(failed(ReadingStatus::Unreachable, T0), T0);
    TEST_ASSERT_FALSE(closeHold(in, evaluatePark(in), empty).riding);

    for (ReadingStatus s : {ReadingStatus::Waiting, ReadingStatus::NotConfigured})
    {
        NinaMemory m;
        evaluate(m, inputsAt(good(), T0));
        TEST_ASSERT_FALSE(evaluate(m, inputsAt(failed(s, T0 + 1000), T0 + 1000)).riding);
    }
    // A good reading gone stale is not a read failure.
    NinaMemory m;
    TEST_ASSERT_FALSE(evaluate(m, inputsAt(good(), T0 + NINA_FRESH_MS + 1)).riding);
    // A new N.I.N.A. address: the old server's reading proves nothing.
    NinaMemory moved;
    evaluate(moved, inputsAt(good(), T0));
    TEST_ASSERT_FALSE(evaluate(moved, inputsAt(failed(ReadingStatus::Unreachable, T0 + 1000), T0 + 1000), GEN + 1).riding);
}

// --- With the gates: the timeline of a close --------------------------------

void test_bench_timeline_a_three_second_timeout_then_recovery(void)
{
    NinaMemory m;
    ParkInputs in = inputsAt(good(), T0);
    CloseHold h = evaluate(m, in);
    TEST_ASSERT_FALSE(RoofIo::interlockLost(closing(), gatesFrom(in, h, T0 + 500)));
    // "N.I.N.A. not responding: read Timeout" for about 3 s.
    for (uint64_t t = T0 + 6000; t <= T0 + 9000; t += 1000)
    {
        in = inputsAt(failed(ReadingStatus::Unreachable, t), t);
        h = evaluate(m, in);
        TEST_ASSERT_TRUE(h.riding);
        TEST_ASSERT_FALSE(RoofIo::interlockLost(closing(), gatesFrom(in, h, t + 250)));
    }
    in = inputsAt(good(true, false, true, T0 + 9500), T0 + 9600);
    h = evaluate(m, in);
    TEST_ASSERT_TRUE(h.parked);
    TEST_ASSERT_FALSE(RoofIo::interlockLost(closing(), gatesFrom(in, h, T0 + 9800)));
}

void test_silent_nina_stops_the_close_at_the_freshness_bound(void)
{
    NinaMemory m;
    evaluate(m, inputsAt(good(), T0));
    const ParkInputs in = inputsAt(failed(ReadingStatus::Unreachable, T0 + 8000), T0 + 8000);
    const CloseHold h = evaluate(m, in);
    // The loop's last evaluation was at 8 s; the control task's own clock bounds it.
    TEST_ASSERT_FALSE(RoofIo::interlockLost(closing(), gatesFrom(in, h, T0 + NINA_FRESH_MS)));
    const RoofIo::GateInputs late = gatesFrom(in, h, T0 + NINA_FRESH_MS + 1);
    TEST_ASSERT_TRUE(RoofIo::interlockLost(closing(), late));
    TEST_ASSERT_EQUAL_UINT32(RoofIo::InterlockNinaStale, RoofIo::interlockBits(late));
}

void test_a_good_reading_is_bounded_by_the_control_task_too(void)
{
    NinaMemory m;
    const ParkInputs in = inputsAt(good(), T0 + 9900); // the close started on a 9.9 s old reading
    const CloseHold h = evaluate(m, in);
    TEST_ASSERT_TRUE(h.parked);
    TEST_ASSERT_FALSE(RoofIo::interlockLost(closing(), gatesFrom(in, h, T0 + NINA_FRESH_MS)));
    TEST_ASSERT_TRUE(RoofIo::interlockLost(closing(), gatesFrom(in, h, T0 + NINA_FRESH_MS + 1)));
}

void test_a_switch_opening_during_the_ride_stops_in_that_step(void)
{
    NinaMemory m;
    evaluate(m, inputsAt(good(), T0, 1));
    const ParkInputs in = inputsAt(failed(ReadingStatus::Unreachable, T0 + 2000), T0 + 2000, 1);
    const CloseHold h = evaluate(m, in);
    TEST_ASSERT_TRUE(h.riding);
    RoofIo::GateInputs g = gatesFrom(in, h, T0 + 2100);
    TEST_ASSERT_FALSE(RoofIo::interlockLost(closing(), g));
    g.switch1 = RoofIo::Contact::Open; // the control task's own reading, every step
    TEST_ASSERT_TRUE(RoofIo::interlockLost(closing(), g));
    TEST_ASSERT_EQUAL_UINT32(RoofIo::InterlockSwitch1, RoofIo::interlockBits(g));
}

void test_the_ride_never_starts_a_close(void)
{
    NinaMemory m;
    evaluate(m, inputsAt(good(), T0));
    const ParkInputs in = inputsAt(failed(ReadingStatus::Unreachable, T0 + 2000), T0 + 2000);
    const RoofIo::GateInputs g = gatesFrom(in, evaluate(m, in), T0 + 2100);
    TEST_ASSERT_TRUE(g.ninaGrace);
    TEST_ASSERT_EQUAL(RoofIo::GateRefusal::MountNotParked, RoofIo::gateCommand(Roof::Command::Close, g));
    TEST_ASSERT_FALSE(RoofIo::parkHolds(g));
}

// --- The gates' rule (RoofGates closeMayContinue) -----------------------------

// Spec 021: a move rides through a missed N.I.N.A. reply only while the
// reading it relies on is fresh, with every fitted switch closed now.
void test_close_may_continue_on_a_fresh_ride_through(void)
{
    RoofIo::GateInputs riding = parkedGates(1);
    riding.parked = false;
    riding.ninaGrace = true;
    riding.parkFreshUntilMs = 60000;
    riding.nowMs = 60000;
    TEST_ASSERT_TRUE(RoofIo::closeMayContinue(riding));
    TEST_ASSERT_FALSE(RoofIo::interlockLost(inState(Roof::State::Closing), riding));
    riding.nowMs = 60001;
    TEST_ASSERT_TRUE(RoofIo::interlockLost(inState(Roof::State::Closing), riding));
    TEST_ASSERT_EQUAL_UINT32(RoofIo::InterlockNinaStale, RoofIo::interlockBits(riding));
    riding.nowMs = 1000;
    riding.switch1 = RoofIo::Contact::Unreadable;
    TEST_ASSERT_TRUE(RoofIo::interlockLost(inState(Roof::State::Closing), riding));
    TEST_ASSERT_EQUAL_UINT32(RoofIo::InterlockSwitch1, RoofIo::interlockBits(riding));
    // A ride-through without a bound handed over never holds.
    RoofIo::GateInputs unbounded = parkedGates(0);
    unbounded.parked = false;
    unbounded.ninaGrace = true;
    TEST_ASSERT_FALSE(RoofIo::closeMayContinue(unbounded));
}

void test_a_parked_reading_is_bounded_during_a_move(void)
{
    RoofIo::GateInputs in = parkedGates(0);
    in.parkFreshUntilMs = 30000;
    in.nowMs = 30000;
    TEST_ASSERT_FALSE(RoofIo::interlockLost(inState(Roof::State::Closing), in));
    in.nowMs = 30001;
    TEST_ASSERT_TRUE(RoofIo::interlockLost(inState(Roof::State::Closing), in));
    TEST_ASSERT_EQUAL_UINT32(RoofIo::InterlockNinaStale, RoofIo::interlockBits(in));
    // The start check is unchanged: the hand-over's park result alone.
    TEST_ASSERT_EQUAL(RoofIo::GateRefusal::None, RoofIo::gateCommand(Roof::Command::Close, in));
}

void test_a_ride_through_never_starts_a_close_and_a_stale_handover_ends_it(void)
{
    RoofIo::GateInputs riding = parkedGates(0);
    riding.parked = false;
    riding.ninaGrace = true;
    riding.parkFreshUntilMs = 60000;
    riding.nowMs = 1000;
    TEST_ASSERT_EQUAL(RoofIo::GateRefusal::MountNotParked, RoofIo::gateCommand(Roof::Command::Close, riding));
    const RoofIo::GateInputs stale = RoofIo::currentGates(riding, true, RoofIo::SAFETY_HANDOVER_STALE_MS + 1);
    TEST_ASSERT_FALSE(stale.ninaGrace);
    TEST_ASSERT_TRUE(RoofIo::interlockLost(inState(Roof::State::Closing), stale));
    TEST_ASSERT_TRUE(RoofIo::currentGates(riding, true, RoofIo::SAFETY_HANDOVER_STALE_MS).ninaGrace);
    Roof::Status leg = inState(Roof::State::Opening);
    leg.calibrating = true;
    TEST_ASSERT_FALSE(RoofIo::interlockLost(leg, riding));
    TEST_ASSERT_TRUE(RoofIo::interlockLost(leg, stale));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_memory_keeps_the_latest_good_reading_across_failures);
    RUN_TEST(test_memory_forgets_on_a_new_address_or_no_reading);
    RUN_TEST(test_parked_holds_until_the_reading_is_stale);
    RUN_TEST(test_each_read_failure_rides_on_the_parked_reading);
    RUN_TEST(test_ride_through_boundary_at_the_freshness_limit);
    RUN_TEST(test_failure_then_not_parked_evidence_stops_the_ride);
    RUN_TEST(test_no_ride_with_a_switch_open_or_unreadable);
    RUN_TEST(test_no_ride_on_a_remembered_reading_that_was_not_parked);
    RUN_TEST(test_no_ride_without_a_memory_or_for_other_reasons);
    RUN_TEST(test_bench_timeline_a_three_second_timeout_then_recovery);
    RUN_TEST(test_silent_nina_stops_the_close_at_the_freshness_bound);
    RUN_TEST(test_a_good_reading_is_bounded_by_the_control_task_too);
    RUN_TEST(test_a_switch_opening_during_the_ride_stops_in_that_step);
    RUN_TEST(test_the_ride_never_starts_a_close);
    RUN_TEST(test_close_may_continue_on_a_fresh_ride_through);
    RUN_TEST(test_a_parked_reading_is_bounded_during_a_move);
    RUN_TEST(test_a_ride_through_never_starts_a_close_and_a_stale_handover_ends_it);
    return UNITY_END();
}
