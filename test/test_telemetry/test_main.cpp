// Spec 046: MQTT telemetry (lib/DiagLogic Telemetry). The 1 Hz cadence while
// the roof moves and its tail, the supply at rest and its 0.2 V rule, nothing
// when off, one move record per finished move with its fields, and every
// document inside its fixed buffer.

#include <unity.h>

#include <ArduinoJson.h>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "MoveLog.h"
#include "Telemetry.h"

using namespace SQM;
using namespace SQM::Telemetry;

void setUp() {}
void tearDown() {}

namespace
{
    constexpr uint32_t CHECK_MS = 100; // WebServer's state check
    constexpr int64_t EPOCH_MS = 1764150000000LL;

    struct Published
    {
        uint32_t atMs;
        Kind kind;
    };

    // Runs the schedule at the state check's rate, as WebServer does, with
    // every due document published.
    struct Run
    {
        Schedule schedule;
        uint32_t now = 5000;
        std::vector<Published> out;

        void step(bool moving, float volts)
        {
            const Kind kind = schedule.due(moving, volts, now);
            if (kind != Kind::None)
            {
                out.push_back({now, kind});
                schedule.published(kind, volts, now);
            }
            now += CHECK_MS;
        }

        void run(uint32_t ms, bool moving, float volts)
        {
            const uint32_t end = now + ms;
            while (now < end)
                step(moving, volts);
        }

        size_t count(Kind kind, uint32_t fromMs = 0, uint32_t toMs = UINT32_MAX) const
        {
            size_t n = 0;
            for (const Published &p : out)
                if (p.kind == kind && p.atMs >= fromMs && p.atMs < toMs)
                    ++n;
            return n;
        }
    };

    DynamicJsonDocument parse(const char *text, size_t length)
    {
        DynamicJsonDocument doc(2048);
        TEST_ASSERT_FALSE(deserializeJson(doc, text, length));
        return doc;
    }

    Diag::Summary finishedMove()
    {
        Diag::Summary m;
        m.id = 7;
        m.startMs = 10000;
        m.endMs = 67000;
        m.drivenMs = 56000;
        m.origin = Roof::Origin::Automatic;
        m.target = Roof::End::Closed;
        m.endState = Roof::State::Closed;
        m.result = Diag::Result::Arrived;
        m.hallStart = 2900;
        m.hallEnd = 1050;
        m.hallPeak = 1010;
        m.hallPeakMs = 56500;
        m.restVolts = 12.61f;
        m.minVolts = 11.9f;
        m.peakAmps = 6.4f;
        m.averageAmps = 3.25f;
        m.blind = true;
        return m;
    }
} // namespace

// While the roof moves: at once when it starts, then once a second, and
// TAIL_MS after it stops; then nothing until the rest interval.
void test_about_once_a_second_while_moving_and_the_tail()
{
    Run r;
    r.schedule.configure(true, 60);
    r.run(2000, false, 12.6f); // the first rest sample at once
    TEST_ASSERT_EQUAL(1, r.count(Kind::Rest));
    const uint32_t start = r.now;
    r.run(57000, true, 12.0f);
    const uint32_t stop = r.now;
    r.run(20000, false, 12.6f);
    TEST_ASSERT_EQUAL_UINT32(start, r.out[1].atMs); // the move's first check
    TEST_ASSERT_EQUAL(Kind::Moving, r.out[1].kind);
    // 57 s at 1 Hz, then the 3 s tail.
    TEST_ASSERT_EQUAL(57, r.count(Kind::Moving, start, stop));
    TEST_ASSERT_EQUAL(3, r.count(Kind::Moving, stop, stop + TAIL_MS + CHECK_MS));
    TEST_ASSERT_EQUAL(0, r.count(Kind::Moving, stop + TAIL_MS + CHECK_MS));
    for (size_t i = 2; i < r.out.size(); ++i)
        if (r.out[i].kind == Kind::Moving)
            TEST_ASSERT_TRUE(r.out[i].atMs - r.out[i - 1].atMs >= MOVING_INTERVAL_MS);
    // No rest sample in the 20 s after (the supply came back to where it was
    // left by the tail): only the interval brings one.
    TEST_ASSERT_EQUAL(1, r.count(Kind::Rest));
}

// At rest the supply alone, every restVoltsSeconds; a full hour gives 60.
void test_rest_supply_every_interval()
{
    Run r;
    r.schedule.configure(true, 60);
    r.run(3600000, false, 12.6f);
    TEST_ASSERT_EQUAL(60, r.count(Kind::Rest));
    TEST_ASSERT_EQUAL(0, r.count(Kind::Moving));
    for (size_t i = 1; i < r.out.size(); ++i)
        TEST_ASSERT_EQUAL_UINT32(60000, r.out[i].atMs - r.out[i - 1].atMs);
}

void test_rest_interval_zero_publishes_nothing_at_rest_but_moves_still_go()
{
    Run r;
    r.schedule.configure(true, 0);
    r.run(600000, false, 12.6f);
    r.run(600000, false, 11.0f); // a big change: still nothing
    TEST_ASSERT_EQUAL(0, r.out.size());
    r.run(5000, true, 12.0f);
    TEST_ASSERT_EQUAL(5, r.count(Kind::Moving));
}

// A change of 0.2 V or more goes out before the interval, but no sooner than
// VOLTS_CHANGE_MIN_MS after the last; a smaller one waits for the interval.
void test_volts_change_rule()
{
    Run r;
    r.schedule.configure(true, 60);
    r.step(false, 12.60f); // first
    const uint32_t first = r.out[0].atMs;
    r.run(20000, false, 12.45f); // 0.15 V: not a change
    TEST_ASSERT_EQUAL(1, r.out.size());
    r.run(1000, false, 12.40f); // 0.2 V from the last published, 21 s later
    TEST_ASSERT_EQUAL(2, r.out.size());
    TEST_ASSERT_EQUAL(Kind::Rest, r.out[1].kind);
    TEST_ASSERT_EQUAL_UINT32(first + CHECK_MS + 20000, r.out[1].atMs); // the first check at 12.40 V

    // A step 2 s after a publish waits out the 10 s gap, then goes once.
    const uint32_t last = r.out[1].atMs;
    r.run(2000 - CHECK_MS, false, 12.40f);
    r.run(30000, false, 12.10f);
    TEST_ASSERT_EQUAL(3, r.out.size());
    TEST_ASSERT_EQUAL_UINT32(last + VOLTS_CHANGE_MIN_MS, r.out[2].atMs);

    // A noisy supply flapping 0.3 V every check: never more than one every 10 s.
    Run noisy;
    noisy.schedule.configure(true, 60);
    for (int i = 0; i < 600; ++i) // 60 s
        noisy.step(false, i % 2 == 0 ? 12.6f : 12.3f);
    TEST_ASSERT_TRUE(noisy.out.size() <= 7);

    // The supply becoming unreadable (or readable again) counts as a change.
    Run lost;
    lost.schedule.configure(true, 60);
    lost.step(false, 12.6f);
    lost.run(VOLTS_CHANGE_MIN_MS, false, NAN);
    TEST_ASSERT_EQUAL(2, lost.out.size());
}

void test_nothing_when_off()
{
    Run r;
    r.schedule.configure(false, 60);
    r.run(120000, false, 12.6f);
    r.run(60000, true, 12.0f);
    r.run(10000, false, 11.0f);
    TEST_ASSERT_EQUAL(0, r.out.size());
    // Turned on again: at once.
    r.schedule.configure(true, 60);
    r.step(false, 12.6f);
    TEST_ASSERT_EQUAL(1, r.out.size());
}

// A document the publish dropped (MQTT down, no time left) isn't retried:
// published() is called anyway and the cadence carries on.
void test_a_dropped_document_is_not_queued()
{
    Schedule s;
    s.configure(true, 60);
    TEST_ASSERT_EQUAL(Kind::Moving, s.due(true, 12.0f, 1000));
    s.published(Kind::Moving, 12.0f, 1000);
    TEST_ASSERT_EQUAL(Kind::None, s.due(true, 12.0f, 1100));
    TEST_ASSERT_EQUAL(Kind::Moving, s.due(true, 12.0f, 2000));
}

void test_moving_sample_fields()
{
    Sample s;
    s.state = Roof::State::Closing;
    s.positionKnown = true;
    s.positionPercent = 42.345f;
    s.hall = 2050;
    s.volts = 12.414f;
    s.dutyPercent = 100;
    s.phase = "closing";
    s.target = Roof::End::Closed;
    s.origin = Roof::Origin::Automatic;
    char buf[SAMPLE_BYTES];
    const size_t n = writeSample(buf, sizeof(buf), s, stampOf(EPOCH_MS, 5000, 5000));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING(
        "{\"ts\":1764150000000,\"state\":\"closing\",\"pos\":42.3,\"hall\":2050,\"volts\":12.41,\"duty\":100,"
        "\"phase\":\"closing\",\"target\":\"closed\",\"origin\":\"automatic\"}",
        buf);
    TEST_ASSERT_EQUAL(std::strlen(buf), n);

    // Unknown position and supply: null, never a made-up number.
    s.positionKnown = false;
    s.volts = NAN;
    writeSample(buf, sizeof(buf), s, stampOf(EPOCH_MS, 5000, 5000));
    const DynamicJsonDocument doc = parse(buf, std::strlen(buf));
    TEST_ASSERT_TRUE(doc["pos"].isNull());
    TEST_ASSERT_TRUE(doc["volts"].isNull());
    TEST_ASSERT_EQUAL(9, doc.as<JsonObjectConst>().size());
}

void test_rest_document_and_stamps()
{
    char buf[SAMPLE_BYTES];
    size_t n = writeRest(buf, sizeof(buf), 12.6f, stampOf(EPOCH_MS, 5000, 5000));
    TEST_ASSERT_EQUAL_STRING("{\"ts\":1764150000000,\"volts\":12.60}", buf);
    TEST_ASSERT_EQUAL(std::strlen(buf), n);
    // No clock: the uptime in milliseconds (below 1e12).
    const Stamp uptime = stampOf(0, 5000, 5000);
    TEST_ASSERT_FALSE(uptime.epoch);
    writeRest(buf, sizeof(buf), 12.6f, uptime);
    TEST_ASSERT_EQUAL_STRING("{\"ts\":5000,\"volts\":12.60}", buf);
    // A moment in the past (a move's end) is stamped back from now.
    const Stamp end = stampOf(EPOCH_MS, 70000, 67000);
    TEST_ASSERT_TRUE(end.epoch);
    TEST_ASSERT_EQUAL_UINT64(static_cast<uint64_t>(EPOCH_MS - 3000), end.ms);
    // Too small a buffer: nothing (0), so nothing is published.
    char tiny[10];
    TEST_ASSERT_EQUAL(0, writeRest(tiny, sizeof(tiny), 12.6f, uptime));
}

void test_move_record_fields()
{
    char buf[MOVE_BYTES];
    const Diag::Summary m = finishedMove();
    size_t n = writeMove(buf, sizeof(buf), m, stampOf(EPOCH_MS, 67000, 67000), true);
    TEST_ASSERT_TRUE(n > 0);
    DynamicJsonDocument doc = parse(buf, n);
    const char *keys[] = {"ts",         "id",       "origin",    "target",      "endState",    "result",     "fault",
                          "durationMs", "drivenMs", "hallStart", "hallEnd",     "hallPeak",    "hallPeakMs", "restVolts",
                          "minVolts",   "sagVolts", "peakAmps",  "averageAmps", "fromUnknown", "blind",      "calibration"};
    for (const char *key : keys)
        TEST_ASSERT_TRUE_MESSAGE(doc.containsKey(key), key);
    TEST_ASSERT_EQUAL(sizeof(keys) / sizeof(keys[0]), doc.as<JsonObjectConst>().size());
    TEST_ASSERT_EQUAL_UINT64(1764150000000ULL, doc["ts"].as<uint64_t>());
    TEST_ASSERT_EQUAL(7, doc["id"].as<int>());
    TEST_ASSERT_EQUAL_STRING("automatic", doc["origin"]);
    TEST_ASSERT_EQUAL_STRING("closed", doc["target"]);
    TEST_ASSERT_EQUAL_STRING("closed", doc["endState"]);
    TEST_ASSERT_EQUAL_STRING("arrived", doc["result"]);
    TEST_ASSERT_TRUE(doc["fault"].isNull());
    TEST_ASSERT_EQUAL(57000, doc["durationMs"].as<int>());
    TEST_ASSERT_EQUAL(56000, doc["drivenMs"].as<int>());
    TEST_ASSERT_EQUAL(2900, doc["hallStart"].as<int>());
    TEST_ASSERT_EQUAL(1050, doc["hallEnd"].as<int>());
    TEST_ASSERT_EQUAL(1010, doc["hallPeak"].as<int>());
    TEST_ASSERT_EQUAL(56500, doc["hallPeakMs"].as<int>());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.61f, doc["restVolts"].as<float>());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 11.9f, doc["minVolts"].as<float>());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.71f, doc["sagVolts"].as<float>());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 6.4f, doc["peakAmps"].as<float>());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.25f, doc["averageAmps"].as<float>());
    TEST_ASSERT_FALSE(doc["fromUnknown"].as<bool>());
    TEST_ASSERT_TRUE(doc["blind"].as<bool>());
    TEST_ASSERT_FALSE(doc["calibration"].as<bool>());

    // No current sensor: no amps at all. A fault names its kind.
    Diag::Summary f = m;
    f.result = Diag::Result::Fault;
    f.fault = Roof::FaultKind::NoMovement;
    f.restVolts = NAN;
    n = writeMove(buf, sizeof(buf), f, stampOf(EPOCH_MS, 67000, 67000), false);
    doc = parse(buf, n);
    TEST_ASSERT_FALSE(doc.containsKey("peakAmps"));
    TEST_ASSERT_FALSE(doc.containsKey("averageAmps"));
    TEST_ASSERT_EQUAL_STRING("fault", doc["result"]);
    TEST_ASSERT_EQUAL_STRING("noMovement", doc["fault"]);
    TEST_ASSERT_TRUE(doc["restVolts"].isNull());
    TEST_ASSERT_TRUE(doc["sagVolts"].isNull());
}

// Every field at its longest still fits each buffer.
void test_longest_documents_fit()
{
    Sample s;
    s.state = Roof::State::Unknown;
    s.positionKnown = true;
    s.positionPercent = -100.0f;
    s.hall = 65535;
    s.volts = -655.34f;
    s.dutyPercent = 255;
    s.phase = "heldByPerson";
    s.target = Roof::End::Closed;
    s.origin = Roof::Origin::Automatic;
    Stamp longest;
    longest.ms = UINT64_MAX;
    char buf[MOVE_BYTES];
    TEST_ASSERT_TRUE(writeSample(buf, SAMPLE_BYTES, s, longest) > 0);
    TEST_ASSERT_TRUE(writeRest(buf, SAMPLE_BYTES, -655.34f, longest) > 0);

    Diag::Summary m = finishedMove();
    m.id = UINT32_MAX;
    m.startMs = 0;
    m.endMs = UINT32_MAX;
    m.drivenMs = UINT32_MAX;
    m.hallPeakMs = UINT32_MAX;
    m.hallStart = m.hallEnd = m.hallPeak = 65535;
    m.restVolts = m.minVolts = m.peakAmps = m.averageAmps = -655.34f;
    m.minVolts = 655.34f;
    m.result = Diag::Result::Fault;
    m.fault = Roof::FaultKind::CurrentUnreadable;
    m.endState = Roof::State::Unknown;
    m.origin = Roof::Origin::Automatic;
    m.fromUnknown = m.blind = m.calibration = false;
    const size_t n = writeMove(buf, MOVE_BYTES, m, longest, true);
    TEST_ASSERT_TRUE(n > 0);
    parse(buf, n);

    // A phase name that isn't a plain name is never written unescaped.
    s.phase = "a\"b";
    writeSample(buf, SAMPLE_BYTES, s, longest);
    const DynamicJsonDocument doc = parse(buf, std::strlen(buf));
    TEST_ASSERT_EQUAL_STRING("?", doc["phase"]);
}

void test_move_cue_once_per_move_and_dropped_across_a_reconnect()
{
    MoveCue cue;
    TEST_ASSERT_FALSE(cue.newMove(3, 1)); // primes: nothing from before
    TEST_ASSERT_FALSE(cue.newMove(3, 1));
    TEST_ASSERT_TRUE(cue.newMove(4, 1));  // a move finished
    TEST_ASSERT_FALSE(cue.newMove(4, 1)); // once
    TEST_ASSERT_FALSE(cue.newMove(5, 2)); // finished while MQTT was down: dropped
    TEST_ASSERT_TRUE(cue.newMove(6, 2));
    TEST_ASSERT_FALSE(cue.newMove(0, 3)); // diagnostics turned off and on (a new log)
    TEST_ASSERT_TRUE(cue.newMove(1, 3));
}

// The move log's own summary, three moves in a row: one record each, with the
// blind flag the move carried.
void test_one_record_per_recorded_move()
{
    Diag::MoveLog log;
    MoveCue cue;
    Diag::Sample s;
    uint32_t now = 0;
    int records = 0;
    std::vector<uint32_t> ids;
    auto run = [&](Roof::State state, bool driving, bool blind, int steps)
    {
        for (int i = 0; i < steps; ++i)
        {
            s.nowMs = now;
            s.status.state = state;
            s.status.driving = driving;
            s.status.blind = blind;
            s.output.duty = driving ? 255 : 0;
            s.output.command = driving ? Driver::Command::Forward : Driver::Command::Off;
            s.supplyVolts = driving ? 12.0f : 12.6f;
            log.step(s);
            now += 20;
            if (now % 1000 == 0 && cue.newMove(log.finished(), 1))
            {
                Diag::Summary latest;
                TEST_ASSERT_TRUE(log.latest(latest));
                TEST_ASSERT_FALSE(latest.active);
                char buf[MOVE_BYTES];
                const size_t n = writeMove(buf, sizeof(buf), latest, stampOf(EPOCH_MS, now, latest.endMs), false);
                TEST_ASSERT_TRUE(n > 0);
                const DynamicJsonDocument doc = parse(buf, n);
                ids.push_back(doc["id"].as<uint32_t>());
                TEST_ASSERT_EQUAL(ids.size() == 2, doc["blind"].as<bool>());
                ++records;
            }
        }
    };
    s.status.target = Roof::End::Open;
    run(Roof::State::Closed, false, false, 100);
    run(Roof::State::Opening, true, false, 500);
    run(Roof::State::Open, false, false, 200);
    s.status.target = Roof::End::Closed;
    run(Roof::State::Closing, true, true, 500);
    run(Roof::State::Closed, false, false, 200);
    s.status.target = Roof::End::Open;
    run(Roof::State::Opening, true, false, 300);
    run(Roof::State::Stopped, false, false, 200);
    TEST_ASSERT_EQUAL(3, records);
    TEST_ASSERT_EQUAL(3, ids.size());
    TEST_ASSERT_TRUE(ids[0] < ids[1] && ids[1] < ids[2]);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_about_once_a_second_while_moving_and_the_tail);
    RUN_TEST(test_rest_supply_every_interval);
    RUN_TEST(test_rest_interval_zero_publishes_nothing_at_rest_but_moves_still_go);
    RUN_TEST(test_volts_change_rule);
    RUN_TEST(test_nothing_when_off);
    RUN_TEST(test_a_dropped_document_is_not_queued);
    RUN_TEST(test_moving_sample_fields);
    RUN_TEST(test_rest_document_and_stamps);
    RUN_TEST(test_move_record_fields);
    RUN_TEST(test_longest_documents_fit);
    RUN_TEST(test_move_cue_once_per_move_and_dropped_across_a_reconnect);
    RUN_TEST(test_one_record_per_recorded_move);
    return UNITY_END();
}
