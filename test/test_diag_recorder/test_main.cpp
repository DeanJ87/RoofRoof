// Spec 017's move recorder (lib/DiagLogic MoveLog): summaries, the profile and
// its merging, the ramp phase, the supply at rest, the stall current, and the
// bounded, allocation-free step (FR-008 to FR-013, FR-020 to FR-022, SC-003 to SC-005).

#include <unity.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <new>
#include <vector>

#include "MotionProfile.h"
#include "MoveLog.h"
#include "RoofController.h"
#include "RoofPlant.h"
#include "SummaryList.h"

using namespace SQM;
using namespace SQM::Diag;

// Every allocation in this test binary is counted, so a step can be shown to allocate nothing.
static std::atomic<size_t> allocations{0};
void *operator new(std::size_t size)
{
    ++allocations;
    void *p = std::malloc(size == 0 ? 1 : size);
    if (p == nullptr)
        throw std::bad_alloc();
    return p;
}
void operator delete(void *p) noexcept
{
    std::free(p);
}
void operator delete(void *p, std::size_t) noexcept
{
    std::free(p);
}

void setUp() {}
void tearDown() {}

namespace
{
    // The real motion logic driving the simulated roof, recorded as the control task would.
    struct Rig
    {
        Roof::Settings settings;
        Roof::Controller controller;
        RoofPlant::Plant plant;
        MoveLog log;
        uint32_t now = 1000;
        Roof::Origin origin = Roof::Origin::Person;
        std::vector<Phase> phases;

        explicit Rig(bool current = true, float startMm = 0.0f)
        {
            settings.currentSenseEnabled = current;
            controller = Roof::Controller(settings);
            plant = RoofPlant::forSettings(settings, startMm);
            plant.sagVolts = 0.5f;
        }

        Sample sample(const Roof::Readings &r) const
        {
            Sample s;
            s.nowMs = r.nowMs;
            s.status = controller.status();
            s.output = controller.output();
            s.supplyVolts = r.supplyVolts;
            s.motorAmps = settings.currentSenseEnabled && controller.status().driving ? r.motorAmps : NAN;
            if (!settings.currentSenseEnabled)
                s.motorAmps = NAN;
            s.origin = origin;
            s.slowDuty = Roof::toDuty(Roof::dutyFraction(settings.slowDutyPercent));
            s.topDuty = Roof::toDuty(Roof::dutyFraction(settings.topDutyPercent));
            return s;
        }

        void step()
        {
            plant.drive(controller.output(), 20);
            Roof::Readings r = plant.readings(now);
            r.motorAmps = settings.currentSenseEnabled ? plant.senseAmps() : NAN;
            controller.tick(r);
            log.step(sample(r));
            const Phase phase = log.live().phase;
            if (phases.empty() || phases.back() != phase)
                phases.push_back(phase);
            now += 20;
        }

        void steps(int n)
        {
            for (int i = 0; i < n; ++i)
                step();
        }

        // Steps until the roof isn't moving any more (or the limit).
        void untilStill(uint32_t limitMs = 400000)
        {
            const uint32_t end = now + limitMs;
            do
                step();
            while (Diag::moving(controller.status().state) && now < end);
        }
    };

    // Hand-made steps, for exact profile values.
    struct Feed
    {
        MoveLog log;
        uint32_t now = 0;
        Sample s;

        Feed()
        {
            s.status.target = Roof::End::Open;
            s.slowDuty = 102;
            s.topDuty = 255;
            run(Roof::State::Closed, false, 0, NAN, 12.6f, 1); // primes
        }

        void run(Roof::State state, bool driving, uint8_t duty, float amps, float volts, int steps)
        {
            for (int i = 0; i < steps; ++i)
            {
                s.nowMs = now;
                s.status.state = state;
                s.status.driving = driving;
                s.output.duty = duty;
                s.output.command = driving ? Driver::Command::Forward : Driver::Command::Off;
                s.motorAmps = amps;
                s.supplyVolts = volts;
                log.step(s);
                now += 20;
            }
        }

        // A whole move: `ms` driving at `duty`, then arrived open.
        void move(uint32_t ms, float amps = 3.0f, float volts = 12.0f)
        {
            run(Roof::State::Opening, true, 200, amps, volts, static_cast<int>(ms / 20));
            run(Roof::State::Open, false, 0, NAN, 12.6f, 1);
        }
    };

    Move copyOf(const MoveLog &log, uint32_t id)
    {
        Move move;
        TEST_ASSERT_TRUE(log.copy(id, move));
        return move;
    }
} // namespace

void test_a_full_open_is_one_arrived_move()
{
    Rig rig;
    rig.steps(80); // the Hall settles, a rest block completes
    TEST_ASSERT_EQUAL(Roof::State::Closed, rig.controller.status().state);
    TEST_ASSERT_TRUE(rig.controller.command(Roof::Command::Open, Roof::Origin::Person).accepted);
    rig.untilStill();
    TEST_ASSERT_EQUAL(Roof::State::Open, rig.controller.status().state);
    TEST_ASSERT_EQUAL_UINT32(1, rig.log.finished());
    Summary s;
    TEST_ASSERT_TRUE(rig.log.latest(s));
    TEST_ASSERT_EQUAL_UINT32(1, s.id);
    TEST_ASSERT_FALSE(s.active);
    TEST_ASSERT_EQUAL(Result::Arrived, s.result);
    TEST_ASSERT_EQUAL(Roof::End::Open, s.target);
    TEST_ASSERT_EQUAL(Roof::State::Open, s.endState);
    TEST_ASSERT_EQUAL(Roof::Origin::Person, s.origin);
    TEST_ASSERT_EQUAL(Roof::FaultKind::None, s.fault);
    TEST_ASSERT_UINT32_WITHIN(15000, 50000, s.durationMs());
    TEST_ASSERT_TRUE(s.drivenMs > 0 && s.drivenMs <= s.durationMs());
    TEST_ASSERT_EQUAL_UINT16(rig.plant.lowMagnetCounts, s.hallStart);
    TEST_ASSERT_EQUAL_UINT16(rig.plant.highMagnetCounts, s.hallEnd);
    TEST_ASSERT_TRUE(s.peakAmps >= s.averageAmps && s.averageAmps > 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 12.6f, s.restVolts);
    TEST_ASSERT_TRUE(s.minVolts < s.restVolts);
    TEST_ASSERT_TRUE(s.sagVolts() > 0.0f);
    TEST_ASSERT_TRUE(std::isnan(s.stallAmps));
}

void test_a_normal_move_has_80_to_120_points_covering_it()
{
    Rig rig;
    rig.steps(80);
    rig.controller.command(Roof::Command::Open, Roof::Origin::Person);
    rig.untilStill();
    const Move move = copyOf(rig.log, 1);
    TEST_ASSERT_TRUE(move.count >= 80 && move.count <= MAX_POINTS);
    TEST_ASSERT_TRUE(pointStartMs(move, static_cast<uint8_t>(move.count - 1)) + move.intervalMs >= move.summary.durationMs());
    TEST_ASSERT_TRUE(pointStartMs(move, static_cast<uint8_t>(move.count - 1)) <= move.summary.durationMs());
}

void test_the_phases_follow_the_profile()
{
    Rig rig;
    rig.steps(80);
    rig.controller.command(Roof::Command::Open, Roof::Origin::Person);
    rig.untilStill();
    const std::vector<Phase> want = {Phase::Idle, Phase::SoftStart, Phase::Cruise, Phase::Slowing};
    size_t found = 0;
    for (Phase phase : rig.phases)
        if (found < want.size() && phase == want[found])
            ++found;
    TEST_ASSERT_EQUAL_size_t(want.size(), found);
    TEST_ASSERT_EQUAL(Phase::Idle, rig.phases.back());
    for (Phase phase : rig.phases)
        TEST_ASSERT_TRUE(phase != Phase::Slow);
}

void test_the_dead_time_reads_dead_time()
{
    Feed feed;
    feed.run(Roof::State::Opening, false, 0, NAN, 12.6f, 3);
    TEST_ASSERT_EQUAL(Phase::DeadTime, feed.log.live().phase);
    feed.run(Roof::State::Opening, true, 102, 1.0f, 12.0f, 1);
    TEST_ASSERT_EQUAL(Phase::SoftStart, feed.log.live().phase);
    feed.run(Roof::State::Opening, true, 255, 1.0f, 12.0f, 1);
    TEST_ASSERT_EQUAL(Phase::Cruise, feed.log.live().phase);
    feed.run(Roof::State::Opening, true, 180, 1.0f, 12.0f, 1);
    TEST_ASSERT_EQUAL(Phase::Slowing, feed.log.live().phase);
    feed.run(Roof::State::Opening, true, 102, 1.0f, 12.0f, 1);
    TEST_ASSERT_EQUAL(Phase::Creep, feed.log.live().phase);
    feed.run(Roof::State::Open, false, 0, NAN, 12.6f, 1);
    TEST_ASSERT_EQUAL(Phase::Idle, feed.log.live().phase);
}

void test_a_move_from_unknown_reads_slow()
{
    Rig rig(true, 900.0f);
    rig.steps(80);
    TEST_ASSERT_EQUAL(Roof::State::Unknown, rig.controller.status().state);
    TEST_ASSERT_TRUE(rig.controller.command(Roof::Command::Close, Roof::Origin::Person).accepted);
    rig.untilStill();
    bool slow = false;
    for (Phase phase : rig.phases)
        slow = slow || phase == Phase::Slow;
    TEST_ASSERT_TRUE(slow);
    Summary s;
    TEST_ASSERT_TRUE(rig.log.latest(s));
    TEST_ASSERT_TRUE(s.fromUnknown);
}

void test_a_jam_ends_in_a_fault_with_its_stall_current()
{
    Rig rig;
    rig.steps(80);
    rig.controller.command(Roof::Command::Open, Roof::Origin::Automatic);
    rig.origin = Roof::Origin::Automatic;
    rig.steps(400);
    rig.plant.jammed = true;
    rig.untilStill();
    Summary s;
    TEST_ASSERT_TRUE(rig.log.latest(s));
    TEST_ASSERT_EQUAL(Result::Fault, s.result);
    TEST_ASSERT_EQUAL(Roof::FaultKind::OverCurrent, s.fault);
    TEST_ASSERT_TRUE(s.faultMeasured >= s.faultLimit);
    TEST_ASSERT_TRUE(s.stallAmps > rig.settings.maxCurrentAmps);
    TEST_ASSERT_EQUAL(Roof::Origin::Automatic, s.origin);
}

void test_without_current_sense_every_current_is_null()
{
    Rig rig(false);
    rig.steps(80);
    rig.controller.command(Roof::Command::Open, Roof::Origin::Person);
    rig.untilStill();
    const Move move = copyOf(rig.log, 1);
    TEST_ASSERT_TRUE(std::isnan(move.summary.peakAmps));
    TEST_ASSERT_TRUE(std::isnan(move.summary.averageAmps));
    TEST_ASSERT_TRUE(std::isnan(move.summary.stallAmps));
    for (uint8_t i = 0; i < move.count; ++i)
    {
        TEST_ASSERT_EQUAL_UINT16(NONE, move.points[i].ampsAvg);
        TEST_ASSERT_EQUAL_UINT16(NONE, move.points[i].ampsMax);
    }
}

void test_a_stop_part_way_is_stopped()
{
    Rig rig;
    rig.steps(80);
    rig.controller.command(Roof::Command::Open, Roof::Origin::Person);
    rig.steps(500);
    rig.controller.command(Roof::Command::Stop, Roof::Origin::Person);
    rig.steps(5);
    Summary s;
    TEST_ASSERT_TRUE(rig.log.latest(s));
    TEST_ASSERT_EQUAL(Result::Stopped, s.result);
    TEST_ASSERT_EQUAL(Roof::State::Stopped, s.endState);
}

void test_a_move_under_way_when_attached_is_not_recorded()
{
    Rig rig;
    rig.steps(80);
    rig.controller.command(Roof::Command::Open, Roof::Origin::Person);
    rig.steps(200);
    MoveLog late; // attached part-way
    Sample s;
    s.status = rig.controller.status();
    s.nowMs = rig.now;
    late.step(s);
    rig.log = late;
    rig.untilStill();
    Summary latest;
    TEST_ASSERT_FALSE(rig.log.latest(latest));
    TEST_ASSERT_EQUAL_UINT32(0, rig.log.finished());
    rig.steps(80);
    rig.controller.command(Roof::Command::Close, Roof::Origin::Person);
    rig.untilStill();
    TEST_ASSERT_EQUAL_UINT32(1, rig.log.finished());
}

void test_a_reversal_gives_two_moves()
{
    Feed feed;
    feed.run(Roof::State::Opening, true, 200, 3.0f, 12.0f, 50);
    feed.run(Roof::State::Closing, true, 200, 3.0f, 12.0f, 50);
    feed.run(Roof::State::Closed, false, 0, NAN, 12.6f, 1);
    TEST_ASSERT_EQUAL_UINT32(2, feed.log.finished());
    TEST_ASSERT_EQUAL(Result::Stopped, copyOf(feed.log, 1).summary.result);
}

void test_the_log_keeps_the_last_five_moves()
{
    Feed feed;
    for (int i = 0; i < 7; ++i)
        feed.move(1000);
    uint32_t ids[MAX_MOVES];
    TEST_ASSERT_EQUAL_UINT8(5, feed.log.ids(ids));
    const uint32_t want[] = {7, 6, 5, 4, 3};
    TEST_ASSERT_EQUAL_UINT32_ARRAY(want, ids, 5);
    Move move;
    TEST_ASSERT_FALSE(feed.log.copy(2, move));
    TEST_ASSERT_TRUE(feed.log.copy(3, move));
}

void test_ids_continue_from_set_next_id()
{
    Feed feed;
    feed.log.setNextId(41);
    feed.move(500);
    Summary s;
    TEST_ASSERT_TRUE(feed.log.latest(s));
    TEST_ASSERT_EQUAL_UINT32(41, s.id);
    TEST_ASSERT_EQUAL_UINT32(42, feed.log.nextId());
}

void test_a_move_in_progress_is_readable()
{
    Feed feed;
    feed.run(Roof::State::Opening, true, 200, 3.0f, 12.0f, 60);
    Summary s;
    TEST_ASSERT_TRUE(feed.log.latest(s));
    TEST_ASSERT_TRUE(s.active);
    const Move move = copyOf(feed.log, s.id);
    TEST_ASSERT_EQUAL_UINT8(4, move.count); // 1.2 s: four whole 250 ms points
    TEST_ASSERT_TRUE(feed.log.live().moveActive);
    TEST_ASSERT_EQUAL_UINT32(s.id, feed.log.live().moveId);
}

void test_a_long_move_still_fits()
{
    Feed feed;
    feed.move(180000);
    const Move move = copyOf(feed.log, 1);
    TEST_ASSERT_TRUE(move.count <= MAX_POINTS);
    TEST_ASSERT_EQUAL_UINT32(2000, move.intervalMs);
    TEST_ASSERT_TRUE(static_cast<uint32_t>(move.count) * move.intervalMs >= move.summary.durationMs());
}

void test_merged_points_follow_the_rules()
{
    Feed feed;
    // Interval 0: 1 A, 12 V; interval 1: 3 A, 11 V; then constant to fill 120 points.
    feed.run(Roof::State::Opening, true, 100, 1.0f, 12.0f, 13); // 0-240 ms (and the start step)
    feed.run(Roof::State::Opening, true, 200, 3.0f, 11.0f, 12);
    feed.run(Roof::State::Opening, true, 200, NAN, 12.0f, 12);  // interval 2: no current
    feed.run(Roof::State::Opening, true, 200, 2.0f, 12.0f, 12); // interval 3
    feed.run(Roof::State::Opening, true, 200, 2.0f, 12.0f, 1500);
    const Move move = copyOf(feed.log, 1);
    TEST_ASSERT_EQUAL_UINT32(500, move.intervalMs);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.0f, pointAmpsAvg(move.points[0]));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.0f, pointAmpsMax(move.points[0]));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 11.0f, pointVolts(move.points[0]));
    TEST_ASSERT_EQUAL_UINT8(150, move.points[0].duty);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.0f, pointAmpsAvg(move.points[1])); // the missing side takes the other's
}

void test_a_late_step_leaves_an_empty_point()
{
    Feed feed;
    feed.run(Roof::State::Opening, true, 200, 3.0f, 12.0f, 5);
    feed.now += 600;
    feed.run(Roof::State::Opening, true, 200, 3.0f, 12.0f, 20);
    const Move move = copyOf(feed.log, 1);
    TEST_ASSERT_TRUE(move.count >= 3);
    TEST_ASSERT_EQUAL_UINT16(NONE, move.points[1].ampsAvg);
    TEST_ASSERT_EQUAL_UINT16(NONE, move.points[1].voltsMin);
}

void test_rest_volts_come_from_a_full_block_with_the_motor_off()
{
    MoveLog log;
    Sample s;
    s.supplyVolts = 12.2f;
    for (uint32_t t = 0; t < 600; t += 20)
    {
        s.nowMs = t;
        log.step(s);
    }
    TEST_ASSERT_TRUE(std::isnan(log.live().restVolts));
    for (uint32_t t = 600; t <= 1100; t += 20)
    {
        s.nowMs = t;
        log.step(s);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.2f, log.live().restVolts);
    s.status.state = Roof::State::Opening;
    s.status.driving = true;
    s.supplyVolts = 11.7f;
    s.nowMs = 1120;
    log.step(s);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, log.live().sagVolts);
    s.supplyVolts = NAN;
    s.nowMs = 1140;
    log.step(s);
    TEST_ASSERT_TRUE(std::isnan(log.live().sagVolts));
}

void test_clear_drops_every_move_and_keeps_the_ids()
{
    Feed feed;
    feed.move(500);
    feed.move(500);
    feed.log.clear();
    Summary s;
    TEST_ASSERT_FALSE(feed.log.latest(s));
    uint32_t ids[MAX_MOVES];
    TEST_ASSERT_EQUAL_UINT8(0, feed.log.ids(ids));
    feed.move(500);
    TEST_ASSERT_TRUE(feed.log.latest(s));
    TEST_ASSERT_EQUAL_UINT32(3, s.id);
}

void test_step_work_is_bounded_and_allocates_nothing()
{
    Feed feed;
    const size_t before = allocations.load();
    double worstUs = 0.0;
    for (int i = 0; i < 9000; ++i) // 180 s: four merges
    {
        feed.s.nowMs = feed.now;
        feed.s.status.state = Roof::State::Opening;
        feed.s.status.driving = true;
        feed.s.output.duty = 200;
        feed.s.motorAmps = 3.0f;
        feed.s.supplyVolts = 12.0f;
        const auto start = std::chrono::steady_clock::now();
        feed.log.step(feed.s);
        const auto end = std::chrono::steady_clock::now();
        worstUs = std::max(worstUs, std::chrono::duration<double, std::micro>(end - start).count());
        feed.now += 20;
    }
    feed.run(Roof::State::Open, false, 0, NAN, 12.6f, 1);
    TEST_ASSERT_EQUAL_size_t(before, allocations.load());
    TEST_ASSERT_TRUE_MESSAGE(worstUs < 50.0, "a step took over 50 us on the host");
}

void test_the_log_fits_its_memory_budget()
{
    TEST_ASSERT_TRUE(sizeof(MoveLog) + sizeof(SummaryList) <= 8192);
    TEST_ASSERT_EQUAL_size_t(8, sizeof(Point));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_full_open_is_one_arrived_move);
    RUN_TEST(test_a_normal_move_has_80_to_120_points_covering_it);
    RUN_TEST(test_the_phases_follow_the_profile);
    RUN_TEST(test_the_dead_time_reads_dead_time);
    RUN_TEST(test_a_move_from_unknown_reads_slow);
    RUN_TEST(test_a_jam_ends_in_a_fault_with_its_stall_current);
    RUN_TEST(test_without_current_sense_every_current_is_null);
    RUN_TEST(test_a_stop_part_way_is_stopped);
    RUN_TEST(test_a_move_under_way_when_attached_is_not_recorded);
    RUN_TEST(test_a_reversal_gives_two_moves);
    RUN_TEST(test_the_log_keeps_the_last_five_moves);
    RUN_TEST(test_ids_continue_from_set_next_id);
    RUN_TEST(test_a_move_in_progress_is_readable);
    RUN_TEST(test_a_long_move_still_fits);
    RUN_TEST(test_merged_points_follow_the_rules);
    RUN_TEST(test_a_late_step_leaves_an_empty_point);
    RUN_TEST(test_rest_volts_come_from_a_full_block_with_the_motor_off);
    RUN_TEST(test_clear_drops_every_move_and_keeps_the_ids);
    RUN_TEST(test_step_work_is_bounded_and_allocates_nothing);
    RUN_TEST(test_the_log_fits_its_memory_budget);
    return UNITY_END();
}
