// The roof hardware layer's reading, input, watchdog and stored-fault rules
// (spec 003 FR-001 to FR-004, FR-007, FR-011, FR-012; constitution I: a
// restart never clears the lockout, a missed deadline stops the motor).

#include <cmath>
#include <cstring>
#include <unity.h>

#include "Config.h" // RoofIoLogic builds lib/ConfigModel (SettingsGuard); name it so the native build finds its dependencies
#include "FaultRecord.h"
#include "RoofReadings.h"
#include "StepWatchdog.h"
#include "SwitchInput.h"

using namespace SQM;
using namespace SQM::RoofIo;

void setUp(void) {}
void tearDown(void) {}

// --- Readings ---------------------------------------------------------------

void test_median_of_odd_and_even_counts(void)
{
    const uint16_t odd[] = {900, 100, 500, 300, 700};
    TEST_ASSERT_EQUAL_UINT16(500, medianCounts(odd, 5));
    const uint16_t even[] = {40, 10, 30, 20};
    TEST_ASSERT_EQUAL_UINT16(20, medianCounts(even, 4)); // the lower middle
    const uint16_t one[] = {1234};
    TEST_ASSERT_EQUAL_UINT16(1234, medianCounts(one, 1));
    TEST_ASSERT_EQUAL_UINT16(0, medianCounts(odd, 0));
    TEST_ASSERT_EQUAL_UINT16(0, medianCounts(nullptr, 5));
}

void test_median_rejects_a_spike_and_keeps_a_rail(void)
{
    const uint16_t spike[] = {2000, 2001, 4095, 1999, 2000};
    TEST_ASSERT_EQUAL_UINT16(2000, medianCounts(spike, HALL_SAMPLES));
    // A disconnected sensor pulled to 0 by its divider stays a rail reading.
    const uint16_t rail[] = {0, 3, 0, 1, 2000};
    TEST_ASSERT_LESS_OR_EQUAL_UINT16(Roof::HALL_RAIL_LOW_COUNTS, medianCounts(rail, HALL_SAMPLES));
}

void test_median_uses_at_most_the_sample_limit(void)
{
    uint16_t many[12];
    for (uint16_t i = 0; i < 12; ++i)
        many[i] = static_cast<uint16_t>(i < MAX_SAMPLES ? 100 : 4000);
    TEST_ASSERT_EQUAL_UINT16(100, medianCounts(many, 12));
}

void test_average_millivolts(void)
{
    const uint32_t mv[] = {2000, 2200, 2100, 2100};
    TEST_ASSERT_EQUAL_FLOAT(2100.0f, averageMillivolts(mv, ANALOG_SAMPLES));
    TEST_ASSERT_TRUE(std::isnan(averageMillivolts(mv, 0)));
}

void test_supply_volts_through_the_divider(void)
{
    // 12.0 V into 100k/22k is 2164 mV at the pin.
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 12.0f, supplyVolts(12000.0f * 22.0f / 122.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, supplyVolts(0.0f)); // a disconnected divider reads 0 V: supply low
    TEST_ASSERT_TRUE(std::isnan(supplyVolts(NAN)));
}

void test_motor_amps_takes_the_larger_side(void)
{
    // 1.0 V at IS is 0.667 V after the 10k/20k divider; at 8.5 A/V that's 8.5 A.
    const float pinMv = 1000.0f * 20.0f / 30.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 8.5f, motorAmps(pinMv, 5.0f, 8.5f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 8.5f, motorAmps(5.0f, pinMv, 8.5f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 17.0f, motorAmps(pinMv, 0.0f, 17.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 8.5f, motorAmps(NAN, pinMv, 8.5f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 8.5f, motorAmps(pinMv, NAN, 8.5f));
    TEST_ASSERT_TRUE(std::isnan(motorAmps(NAN, NAN, 8.5f)));
}

// --- Switch inputs ----------------------------------------------------------

void test_switch_unreadable_until_confirmed(void)
{
    SwitchInput in;
    TEST_ASSERT_EQUAL(Contact::Unreadable, in.state());
    TEST_ASSERT_EQUAL(Contact::Unreadable, in.update(true, 0));
    TEST_ASSERT_EQUAL(Contact::Unreadable, in.update(true, 20));
    TEST_ASSERT_EQUAL(Contact::Closed, in.update(true, 40)); // three lows: closed
    SwitchInput high;
    high.update(false, 0);
    high.update(false, 20);
    TEST_ASSERT_EQUAL(Contact::Open, high.update(false, 40)); // three highs: open
}

void test_switch_single_flip_keeps_the_level(void)
{
    SwitchInput in;
    for (uint32_t t = 0; t <= 100; t += 20)
        in.update(true, t);
    TEST_ASSERT_EQUAL(Contact::Closed, in.update(false, 120));
    TEST_ASSERT_EQUAL(Contact::Closed, in.update(true, 140));
    TEST_ASSERT_EQUAL(Contact::Closed, in.update(false, 160));
    TEST_ASSERT_EQUAL(Contact::Closed, in.update(false, 180));
    TEST_ASSERT_EQUAL(Contact::Open, in.update(false, 200)); // three highs in a row
}

void test_switch_chatter_is_unreadable(void)
{
    SwitchInput in;
    in.update(true, 0);
    in.update(true, 20);
    in.update(true, 40); // confirmed at 40
    uint32_t t = 60;
    bool low = false;
    for (; t <= 40 + INPUT_CHATTER_MS; t += 20, low = !low)
        TEST_ASSERT_EQUAL_MESSAGE(Contact::Closed, in.update(low, t), "at the chatter limit: not yet");
    TEST_ASSERT_EQUAL(Contact::Unreadable, in.update(low, t)); // past it
    // Recovers after three equal readings in a row.
    TEST_ASSERT_EQUAL(Contact::Unreadable, in.update(false, t + 20)); // at most two highs in a row
    TEST_ASSERT_EQUAL(Contact::Unreadable, in.update(true, t + 40));
    TEST_ASSERT_EQUAL(Contact::Unreadable, in.update(true, t + 60));
    TEST_ASSERT_EQUAL(Contact::Closed, in.update(true, t + 80));
}

void test_switch_across_the_wrap(void)
{
    SwitchInput in;
    const uint32_t start = 0xFFFFFFFFu - 30;
    in.update(true, start);
    in.update(true, start + 20);
    TEST_ASSERT_EQUAL(Contact::Closed, in.update(true, start + 40)); // wrapped
    TEST_ASSERT_EQUAL(Contact::Closed, in.update(false, start + 60));
    TEST_ASSERT_EQUAL(Contact::Closed, in.update(true, start + 80));
}

void test_switch_mappings(void)
{
    TEST_ASSERT_EQUAL(Safety::SwitchReading::Closed, parkReading(Contact::Closed));
    TEST_ASSERT_EQUAL(Safety::SwitchReading::Open, parkReading(Contact::Open));
    TEST_ASSERT_EQUAL(Safety::SwitchReading::Unreadable, parkReading(Contact::Unreadable));
    TEST_ASSERT_EQUAL(Safety::RainContact::Raining, rainReading(Contact::Closed));
    TEST_ASSERT_EQUAL(Safety::RainContact::Dry, rainReading(Contact::Open));
    TEST_ASSERT_EQUAL(Safety::RainContact::Unreadable, rainReading(Contact::Unreadable));
    TEST_ASSERT_EQUAL_STRING("closed", contactName(Contact::Closed));
    TEST_ASSERT_EQUAL_STRING("open", contactName(Contact::Open));
    TEST_ASSERT_EQUAL_STRING("unreadable", contactName(Contact::Unreadable));
}

// --- Watchdog ---------------------------------------------------------------

void test_watchdog_trips_only_above_the_limit(void)
{
    TEST_ASSERT_FALSE(watchdogExpired(1100, 1000));
    TEST_ASSERT_TRUE(watchdogExpired(1101, 1000));
    TEST_ASSERT_EQUAL_UINT32(100, WATCHDOG_LIMIT_MS);
    TEST_ASSERT_EQUAL_UINT32(Roof::MAX_STEP_GAP_MS, WATCHDOG_LIMIT_MS);
    TEST_ASSERT_TRUE(watchdogExpired(1051, 1000, 50));
}

void test_watchdog_across_the_wrap(void)
{
    const uint32_t last = 0xFFFFFFFFu - 40;
    TEST_ASSERT_FALSE(watchdogExpired(last + 100, last));
    TEST_ASSERT_TRUE(watchdogExpired(last + 101, last));
    TEST_ASSERT_EQUAL_UINT32(101, stepGapMs(last + 101, last));
}

void test_watchdog_step_after_now_is_no_gap(void)
{
    // The control task stamped a step between the timer's two reads.
    TEST_ASSERT_EQUAL_UINT32(0, stepGapMs(1000, 1003));
    TEST_ASSERT_FALSE(watchdogExpired(1000, 1003));
}

// --- Stored fault -----------------------------------------------------------

void test_fault_record_round_trips_every_kind_and_cause(void)
{
    for (uint8_t kind = 1; kind <= static_cast<uint8_t>(Roof::FaultKind::External); ++kind)
    {
        for (uint8_t cause = 0; cause <= static_cast<uint8_t>(FaultCause::BenchSupply); ++cause)
        {
            FaultRecord in;
            in.fault.kind = static_cast<Roof::FaultKind>(kind);
            in.fault.measured = 10.9f + kind;
            in.fault.limit = -3.25f * cause;
            in.cause = static_cast<FaultCause>(cause);
            uint8_t bytes[FAULT_RECORD_BYTES];
            encodeFault(in, bytes);
            FaultRecord out;
            TEST_ASSERT_EQUAL(Decoded::Fault, decodeFault(bytes, sizeof(bytes), out));
            TEST_ASSERT_EQUAL(in.fault.kind, out.fault.kind);
            TEST_ASSERT_EQUAL(in.cause, out.cause);
            TEST_ASSERT_EQUAL_FLOAT(in.fault.measured, out.fault.measured);
            TEST_ASSERT_EQUAL_FLOAT(in.fault.limit, out.fault.limit);
        }
    }
}

void test_fault_record_keeps_nan(void)
{
    FaultRecord in;
    in.fault.kind = Roof::FaultKind::SupplyLow;
    in.fault.measured = NAN; // unreadable supply
    in.fault.limit = 11.0f;
    uint8_t bytes[FAULT_RECORD_BYTES];
    encodeFault(in, bytes);
    FaultRecord out;
    TEST_ASSERT_EQUAL(Decoded::Fault, decodeFault(bytes, sizeof(bytes), out));
    TEST_ASSERT_TRUE(std::isnan(out.fault.measured));
}

void test_empty_store_is_no_fault(void)
{
    FaultRecord out;
    TEST_ASSERT_EQUAL(Decoded::None, decodeFault(nullptr, 0, out));
}

namespace
{
    // A good record with one byte changed must not decode as a fault of its own.
    Decoded decodeWith(size_t index, uint8_t value, bool fixChecksum)
    {
        FaultRecord in;
        in.fault.kind = Roof::FaultKind::Timeout;
        in.fault.measured = 65000.0f;
        in.fault.limit = 65000.0f;
        uint8_t bytes[FAULT_RECORD_BYTES];
        encodeFault(in, bytes);
        bytes[index] = value;
        if (fixChecksum)
        {
            uint16_t sum = 0;
            for (size_t i = 0; i < 14; ++i)
                sum = static_cast<uint16_t>(sum + bytes[i]);
            sum = static_cast<uint16_t>(~sum);
            bytes[14] = static_cast<uint8_t>(sum & 0xFF);
            bytes[15] = static_cast<uint8_t>(sum >> 8);
        }
        FaultRecord out;
        const Decoded result = decodeFault(bytes, sizeof(bytes), out);
        if (result == Decoded::Unreadable)
        {
            TEST_ASSERT_EQUAL(Roof::FaultKind::External, out.fault.kind);
            TEST_ASSERT_EQUAL(FaultCause::StoredUnreadable, out.cause);
        }
        return result;
    }
} // namespace

void test_damaged_record_is_a_fault(void)
{
    TEST_ASSERT_EQUAL(Decoded::Unreadable, decodeWith(0, 'X', true));   // magic
    TEST_ASSERT_EQUAL(Decoded::Unreadable, decodeWith(1, 'X', true));   // magic
    TEST_ASSERT_EQUAL(Decoded::Unreadable, decodeWith(2, 2, true));     // version
    TEST_ASSERT_EQUAL(Decoded::Unreadable, decodeWith(3, 0, true));     // kind None
    TEST_ASSERT_EQUAL(Decoded::Unreadable, decodeWith(3, 11, true));    // kind out of range
    TEST_ASSERT_EQUAL(Decoded::Unreadable, decodeWith(4, 6, true));     // cause out of range
    TEST_ASSERT_EQUAL(Decoded::Unreadable, decodeWith(5, 1, true));     // reserved byte
    TEST_ASSERT_EQUAL(Decoded::Unreadable, decodeWith(7, 0x55, false)); // checksum
    TEST_ASSERT_EQUAL(Decoded::Unreadable, decodeWith(14, 0, false));   // checksum itself
    TEST_ASSERT_EQUAL(Decoded::Fault, decodeWith(7, 0x55, true));       // a changed value that checks out

    uint8_t bytes[FAULT_RECORD_BYTES] = {};
    FaultRecord out;
    TEST_ASSERT_EQUAL(Decoded::Unreadable, decodeFault(bytes, sizeof(bytes) - 1, out)); // short
    TEST_ASSERT_EQUAL(Decoded::Unreadable, decodeFault(bytes, sizeof(bytes), out));     // zeros
    TEST_ASSERT_EQUAL(Decoded::Unreadable, decodeFault(nullptr, 16, out));
}

void test_external_records_and_cause_names(void)
{
    const FaultRecord w = externalRecord(FaultCause::Watchdog, 140.0f, 100.0f);
    TEST_ASSERT_EQUAL(Roof::FaultKind::External, w.fault.kind);
    TEST_ASSERT_EQUAL_FLOAT(140.0f, w.fault.measured);
    TEST_ASSERT_EQUAL(FaultCause::StoredUnreadable, storedUnreadableRecord().cause);
    TEST_ASSERT_EQUAL_STRING("logic", causeName(FaultCause::Logic));
    TEST_ASSERT_EQUAL_STRING("watchdog", causeName(FaultCause::Watchdog));
    TEST_ASSERT_EQUAL_STRING("parkInterlock", causeName(FaultCause::ParkInterlock));
    TEST_ASSERT_EQUAL_STRING("storedUnreadable", causeName(FaultCause::StoredUnreadable));
    TEST_ASSERT_EQUAL_STRING("watchdogStopped", causeName(FaultCause::WatchdogStopped));
    TEST_ASSERT_EQUAL_STRING("benchSupply", causeName(FaultCause::BenchSupply));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_median_of_odd_and_even_counts);
    RUN_TEST(test_median_rejects_a_spike_and_keeps_a_rail);
    RUN_TEST(test_median_uses_at_most_the_sample_limit);
    RUN_TEST(test_average_millivolts);
    RUN_TEST(test_supply_volts_through_the_divider);
    RUN_TEST(test_motor_amps_takes_the_larger_side);
    RUN_TEST(test_switch_unreadable_until_confirmed);
    RUN_TEST(test_switch_single_flip_keeps_the_level);
    RUN_TEST(test_switch_chatter_is_unreadable);
    RUN_TEST(test_switch_across_the_wrap);
    RUN_TEST(test_switch_mappings);
    RUN_TEST(test_watchdog_trips_only_above_the_limit);
    RUN_TEST(test_watchdog_across_the_wrap);
    RUN_TEST(test_watchdog_step_after_now_is_no_gap);
    RUN_TEST(test_fault_record_round_trips_every_kind_and_cause);
    RUN_TEST(test_fault_record_keeps_nan);
    RUN_TEST(test_empty_store_is_no_fault);
    RUN_TEST(test_damaged_record_is_a_fault);
    RUN_TEST(test_external_records_and_cause_names);
    return UNITY_END();
}
