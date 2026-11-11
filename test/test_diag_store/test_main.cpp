// Spec 017: the summary list, the kept file and when it is written, and the
// MQTT schedule (FR-011, FR-015 to FR-019; SC-006, SC-007).

#include <unity.h>

#include "Config.h"

#include <cstring>
#include <vector>

#include "DiagSchedule.h"
#include "KeptSummaries.h"

using namespace SQM;
using namespace SQM::Diag;

void setUp() {}
void tearDown() {}

namespace
{
    Summary summary(uint32_t id)
    {
        Summary s;
        s.id = id;
        s.startMs = 10000 + id * 100000;
        s.endMs = s.startMs + 50500;
        s.drivenMs = 49900;
        s.origin = Roof::Origin::Mqtt;
        s.target = Roof::End::Open;
        s.endState = Roof::State::Error;
        s.result = Result::Fault;
        s.fault = Roof::FaultKind::OverCurrent;
        s.faultMeasured = 12.5f;
        s.faultLimit = 10.0f;
        s.peakAmps = 12.5f;
        s.averageAmps = 3.3f;
        s.stallAmps = 11.9f;
        s.restVolts = 12.62f;
        s.minVolts = NAN;
        s.hallStart = 700;
        s.hallEnd = 2050;
        s.fromUnknown = true;
        s.calibration = id % 2 == 0;
        return s;
    }

    std::vector<uint8_t> encoded(const SummaryList &list, uint32_t nextId, const Clock &clock)
    {
        std::vector<uint8_t> bytes(KEPT_MAX_BYTES);
        const size_t size = encodeKept(list, nextId, clock, bytes.data(), bytes.size());
        TEST_ASSERT_TRUE(size >= KEPT_HEADER_BYTES);
        bytes.resize(size);
        return bytes;
    }
} // namespace

void test_the_list_is_newest_first_and_capped()
{
    SummaryList list;
    for (uint32_t id = 1; id <= 40; ++id)
        list.add(summary(id));
    TEST_ASSERT_EQUAL_UINT8(MAX_SUMMARIES, list.size());
    TEST_ASSERT_EQUAL_UINT32(40, list.at(0).id);
    TEST_ASSERT_EQUAL_UINT32(9, list.at(MAX_SUMMARIES - 1).id);
    TEST_ASSERT_EQUAL_UINT32(40, list.highestId());
    const uint32_t before = list.revision();
    list.clear();
    TEST_ASSERT_EQUAL_UINT8(0, list.size());
    TEST_ASSERT_TRUE(list.revision() != before);
}

void test_a_kept_file_round_trips_every_field()
{
    SummaryList list;
    list.add(summary(3));
    list.add(summary(4));
    const Clock clock{400000, 1762900000};
    const std::vector<uint8_t> bytes = encoded(list, 5, clock);
    TEST_ASSERT_EQUAL_size_t(KEPT_HEADER_BYTES + 2 * KEPT_RECORD_BYTES, bytes.size());
    SummaryList back;
    uint32_t nextId = 0;
    TEST_ASSERT_TRUE(decodeKept(bytes.data(), bytes.size(), back, nextId));
    TEST_ASSERT_EQUAL_UINT32(5, nextId);
    TEST_ASSERT_EQUAL_UINT8(2, back.size());
    const Summary &s = back.at(0);
    const Summary want = summary(4);
    TEST_ASSERT_TRUE(s.kept);
    TEST_ASSERT_EQUAL_UINT32(4, s.id);
    TEST_ASSERT_EQUAL_UINT32(epochOf(clock, want.startMs), s.startEpoch);
    TEST_ASSERT_EQUAL_UINT32(epochOf(clock, want.endMs), s.endEpoch);
    TEST_ASSERT_EQUAL_UINT32(want.durationMs(), s.durationMs());
    TEST_ASSERT_EQUAL_UINT32(want.drivenMs, s.drivenMs);
    TEST_ASSERT_EQUAL(want.origin, s.origin);
    TEST_ASSERT_EQUAL(want.target, s.target);
    TEST_ASSERT_EQUAL(want.endState, s.endState);
    TEST_ASSERT_EQUAL(want.result, s.result);
    TEST_ASSERT_EQUAL(want.fault, s.fault);
    TEST_ASSERT_TRUE(s.fromUnknown);
    TEST_ASSERT_TRUE(s.calibration);
    TEST_ASSERT_FALSE(back.at(1).calibration);
    TEST_ASSERT_EQUAL_FLOAT(want.faultMeasured, s.faultMeasured);
    TEST_ASSERT_EQUAL_FLOAT(want.stallAmps, s.stallAmps);
    TEST_ASSERT_EQUAL_FLOAT(want.restVolts, s.restVolts);
    TEST_ASSERT_TRUE(std::isnan(s.minVolts));
    TEST_ASSERT_EQUAL_UINT16(700, s.hallStart);
    TEST_ASSERT_EQUAL_UINT16(2050, s.hallEnd);
    // Kept summaries keep their epochs when written again.
    const std::vector<uint8_t> again = encoded(back, nextId, Clock{5, 0});
    TEST_ASSERT_TRUE(again == bytes);
}

void test_without_a_clock_the_epochs_are_zero()
{
    SummaryList list;
    list.add(summary(1));
    const std::vector<uint8_t> bytes = encoded(list, 2, Clock{400000, 0});
    SummaryList back;
    uint32_t nextId = 0;
    TEST_ASSERT_TRUE(decodeKept(bytes.data(), bytes.size(), back, nextId));
    TEST_ASSERT_EQUAL_UINT32(0, back.at(0).startEpoch);
}

void test_a_damaged_file_is_ignored()
{
    SummaryList list;
    list.add(summary(1));
    list.add(summary(2));
    const std::vector<uint8_t> good = encoded(list, 3, Clock{400000, 1762900000});
    SummaryList out;
    out.add(summary(99));
    uint32_t nextId = 7;
    auto refused = [&](std::vector<uint8_t> bytes)
    {
        TEST_ASSERT_FALSE(decodeKept(bytes.data(), bytes.size(), out, nextId));
        TEST_ASSERT_EQUAL_UINT8(1, out.size());
        TEST_ASSERT_EQUAL_UINT32(7, nextId);
    };
    std::vector<uint8_t> bad = good;
    bad[0] = 'X';
    refused(bad);
    bad = good;
    bad[4] = 2; // version
    refused(bad);
    bad = good;
    bad[KEPT_HEADER_BYTES + 3] ^= 1; // a record byte: the CRC
    refused(bad);
    bad = good;
    bad[5] = 3; // count doesn't match the size
    refused(bad);
    bad = good;
    bad.pop_back();
    refused(bad);
    refused(std::vector<uint8_t>(4, 0));
}

void test_the_next_id_continues_after_the_highest_kept_id()
{
    SummaryList list;
    list.add(summary(12));
    const std::vector<uint8_t> bytes = encoded(list, 3, Clock{});
    SummaryList back;
    uint32_t nextId = 0;
    TEST_ASSERT_TRUE(decodeKept(bytes.data(), bytes.size(), back, nextId));
    TEST_ASSERT_EQUAL_UINT32(13, nextId);
}

void test_an_empty_list_is_a_valid_file()
{
    SummaryList list;
    const std::vector<uint8_t> bytes = encoded(list, 9, Clock{});
    TEST_ASSERT_EQUAL_size_t(KEPT_HEADER_BYTES, bytes.size());
    SummaryList back;
    back.add(summary(1));
    uint32_t nextId = 0;
    TEST_ASSERT_TRUE(decodeKept(bytes.data(), bytes.size(), back, nextId));
    TEST_ASSERT_EQUAL_UINT8(0, back.size());
    TEST_ASSERT_EQUAL_UINT32(9, nextId);
    TEST_ASSERT_TRUE(KEPT_MAX_BYTES <= 2048);
}

void test_a_write_is_due_only_with_a_change_and_the_interval_passed()
{
    KeptWrite write;
    write.start(5);
    TEST_ASSERT_FALSE(write.due(0, 5, true));  // just loaded
    TEST_ASSERT_FALSE(write.due(0, 6, false)); // the setting off
    TEST_ASSERT_TRUE(write.due(0, 6, true));   // the first write
    write.written(1000, 6);
    TEST_ASSERT_FALSE(write.due(2000, 6, true));
    TEST_ASSERT_FALSE(write.due(1000 + KEPT_WRITE_MIN_MS - 1, 7, true));
    TEST_ASSERT_TRUE(write.due(1000 + KEPT_WRITE_MIN_MS, 7, true));
    write.written(1000 + KEPT_WRITE_MIN_MS, 7);
    write.cleared();
    TEST_ASSERT_TRUE(write.due(1000 + KEPT_WRITE_MIN_MS + 1, 8, true)); // a clear: at once
    write.written(1000 + KEPT_WRITE_MIN_MS + 1, 8);
    TEST_ASSERT_FALSE(write.due(1000 + KEPT_WRITE_MIN_MS + 2, 9, true));
    write.cleared();
    TEST_ASSERT_TRUE(write.due(1000 + KEPT_WRITE_MIN_MS + 3, 9, false)); // a clear removes the file with the setting off too
}

void test_nothing_goes_over_mqtt_while_busy()
{
    MqttSchedule schedule;
    SummaryList list;
    list.add(summary(1));
    TEST_ASSERT_FALSE(schedule.liveDue(0, true));
    TEST_ASSERT_EQUAL_INT(-1, schedule.nextSummary(list, true));
    TEST_ASSERT_TRUE(schedule.liveDue(0, false));
    TEST_ASSERT_EQUAL_INT(0, schedule.nextSummary(list, false));
}

void test_the_live_document_goes_every_minute_at_rest()
{
    MqttSchedule schedule;
    schedule.livePublished(1000);
    TEST_ASSERT_FALSE(schedule.liveDue(1000 + MQTT_LIVE_MS - 1, false));
    TEST_ASSERT_TRUE(schedule.liveDue(1000 + MQTT_LIVE_MS, false));
    TEST_ASSERT_FALSE(schedule.liveDue(1000 + MQTT_LIVE_MS, true));
}

void test_each_finished_summary_goes_once_oldest_first()
{
    MqttSchedule schedule;
    SummaryList list;
    Summary kept = summary(1);
    kept.kept = true;
    list.add(kept);
    schedule.start(1);
    list.add(summary(2));
    list.add(summary(3));
    Summary active = summary(4);
    active.active = true;
    list.add(active);
    int next = schedule.nextSummary(list, false);
    TEST_ASSERT_EQUAL_UINT32(2, list.at(static_cast<uint8_t>(next)).id);
    schedule.summaryPublished(2);
    next = schedule.nextSummary(list, false);
    TEST_ASSERT_EQUAL_UINT32(3, list.at(static_cast<uint8_t>(next)).id);
    schedule.summaryPublished(3);
    TEST_ASSERT_EQUAL_INT(-1, schedule.nextSummary(list, false));
}

void test_the_stream_runs_faster_while_moving()
{
    TEST_ASSERT_EQUAL_UINT32(STREAM_MOVING_MS, streamIntervalMs(Roof::State::Opening));
    TEST_ASSERT_EQUAL_UINT32(STREAM_MOVING_MS, streamIntervalMs(Roof::State::Closing));
    TEST_ASSERT_EQUAL_UINT32(STREAM_REST_MS, streamIntervalMs(Roof::State::Stopped));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_list_is_newest_first_and_capped);
    RUN_TEST(test_a_kept_file_round_trips_every_field);
    RUN_TEST(test_without_a_clock_the_epochs_are_zero);
    RUN_TEST(test_a_damaged_file_is_ignored);
    RUN_TEST(test_the_next_id_continues_after_the_highest_kept_id);
    RUN_TEST(test_an_empty_list_is_a_valid_file);
    RUN_TEST(test_a_write_is_due_only_with_a_change_and_the_interval_passed);
    RUN_TEST(test_nothing_goes_over_mqtt_while_busy);
    RUN_TEST(test_the_live_document_goes_every_minute_at_rest);
    RUN_TEST(test_each_finished_summary_goes_once_oldest_first);
    RUN_TEST(test_the_stream_runs_faster_while_moving);
    return UNITY_END();
}
