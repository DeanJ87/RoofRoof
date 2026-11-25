// Spec 029 field report: a loose Hall connector (readings jumping between a
// rail and a mid value at rest) latching the hallRail fault while the board
// kept restarting. The pure part of that path, hammered: the controller's
// latch, a person clearing it again and again, and the fault record the
// control task writes to NVS (src/RoofControl.cpp persistFault writes only
// when the record changes, so the writes are counted the same way here).

#include <cstring>
#include <unity.h>

#include "FaultRecord.h"
#include "RoofController.h"

using namespace SQM;
using namespace SQM::Roof;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    constexpr uint32_t STEP_MS = 20;

    bool sameRecord(const RoofIo::FaultRecord &a, const RoofIo::FaultRecord &b)
    {
        return a.fault.kind == b.fault.kind && a.cause == b.cause &&
               std::memcmp(&a.fault.measured, &b.fault.measured, sizeof(float)) == 0 &&
               std::memcmp(&a.fault.limit, &b.fault.limit, sizeof(float)) == 0;
    }

    // What RoofControl::syncFault keeps: the first fault until it is cleared.
    void sync(RoofIo::FaultRecord &kept, const Status &status)
    {
        if (status.fault.kind == FaultKind::None)
            kept = RoofIo::FaultRecord();
        else if (kept.fault.kind == FaultKind::None)
            kept.fault = status.fault;
    }

    struct Run
    {
        uint32_t steps = 0;
        uint32_t writes = 0; // NVS writes persistFault would make
        uint32_t latches = 0;
        bool drivenWhileFaulted = false;
    };

    // `steps` steps alternating a rail and a mid reading every `flipEvery` steps;
    // a person clears the fault every `clearEvery` steps (0: never).
    Run hammer(uint32_t steps, uint32_t flipEvery, uint32_t clearEvery)
    {
        Controller roof{Settings{}};
        RoofIo::FaultRecord kept;
        RoofIo::FaultRecord saved;
        Run run;
        for (uint32_t i = 0; i < steps; ++i)
        {
            Readings in;
            in.nowMs = 1000 + i * STEP_MS;
            in.hallCounts = (i / flipEvery) % 2 == 0 ? 22 : 2066;
            in.supplyVolts = 12.4f;
            const Output out = roof.tick(in);
            if (roof.status().fault.kind != FaultKind::None && (out.command != Driver::Command::Off || out.duty != 0))
                run.drivenWhileFaulted = true;
            sync(kept, roof.status());
            if (!sameRecord(kept, saved))
            {
                // The bytes written and read back must give the same record.
                uint8_t bytes[RoofIo::FAULT_RECORD_BYTES];
                RoofIo::encodeFault(kept, bytes);
                RoofIo::FaultRecord back;
                if (kept.fault.kind != FaultKind::None)
                {
                    TEST_ASSERT_TRUE(RoofIo::decodeFault(bytes, sizeof(bytes), back) == RoofIo::Decoded::Fault);
                    TEST_ASSERT_TRUE(sameRecord(kept, back));
                }
                saved = kept;
                ++run.writes;
            }
            if (clearEvery != 0 && i % clearEvery == clearEvery - 1)
                roof.clearFault(Origin::Person);
            ++run.steps;
        }
        run.latches = roof.faultCount();
        return run;
    }
} // namespace

void test_flapping_hall_latches_once_without_a_clear()
{
    const Run run = hammer(50000, 1, 0); // 1000 s, a new reading each step
    TEST_ASSERT_EQUAL_UINT32(1, run.latches);
    TEST_ASSERT_EQUAL_UINT32(1, run.writes); // one NVS write, not one per flap
    TEST_ASSERT_FALSE(run.drivenWhileFaulted);
}

void test_slow_flapping_latches_once()
{
    const Run run = hammer(50000, 37, 0);
    TEST_ASSERT_EQUAL_UINT32(1, run.latches);
    TEST_ASSERT_EQUAL_UINT32(1, run.writes);
}

void test_repeated_clears_write_twice_per_cycle_at_most()
{
    // A person clearing every 2 s while the connector keeps flapping: each
    // cycle latches once and writes the fault and its removal, nothing more.
    const Run run = hammer(50000, 3, 100);
    TEST_ASSERT_TRUE(run.latches >= 400);
    TEST_ASSERT_TRUE(run.latches <= 500);
    TEST_ASSERT_TRUE(run.writes <= 2 * run.latches + 1);
    TEST_ASSERT_FALSE(run.drivenWhileFaulted);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_flapping_hall_latches_once_without_a_clear);
    RUN_TEST(test_slow_flapping_latches_once);
    RUN_TEST(test_repeated_clears_write_twice_per_cycle_at_most);
    return UNITY_END();
}
