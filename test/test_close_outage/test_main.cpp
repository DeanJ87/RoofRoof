// Spec 030 (US4, US6 default, FR-009): an outage seen whole. The access point
// goes down, SQMeter turns stale, the weather hold fires and the close is
// wanted while N.I.N.A. can't be read: the park is asked for again and again,
// the critical alert names N.I.N.A., and the roof never closes without the
// park confirmed by park.source. Weather back safe calls the attempt off;
// N.I.N.A. back and parked closes the roof; a restart in the middle carries on.
// The real sequence, weather verdict and park rule, stepped every 100 ms.

#include <cstring>
#include <unity.h>
#include <vector>

#include "CloseSequence.h"
#include "Config.h" // for the library finder: RoofLogic (Roof::State) builds with ConfigModel
#include "WeatherHold.h"

using namespace SQM;
using namespace SQM::CloseSeq;
using Roof::State;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    constexpr uint64_t STEP_MS = 100;
    // CloseOnUnsafe's time base (a week), so a restart's clock starts below the old one.
    constexpr uint64_t BASE_MS = 7ull * 24 * 3600 * 1000;
    constexpr uint64_t MINUTE = 60000;

    struct Tally
    {
        int requests = 0;
        int closes = 0;
        int stops = 0;
        int alerts = 0;
        int started = 0;
        int parkNotConfirmed = 0;
        bool endedSafe = false;
        bool closed = false;
        std::vector<AlertText> texts; // every ParkNotConfirmed's wording, as report() builds it
    };

    // The board: SQMeter over HTTP, N.I.N.A. and the park switches, the hold and the sequence.
    struct Site
    {
        Sequence seq;
        WeatherHold hold;
        Safety::WeatherInputs weather;
        Safety::ParkInputs park;
        uint64_t nowMs = BASE_MS;
        State roof = State::Open;
        uint64_t closingSinceMs = 0;
        bool sqmeterUp = true;
        bool sqmeterSafe = true;
        bool ninaUp = true;
        bool ninaParked = false;
        Tally tally;

        Site()
        {
            weather.http.enabled = true;
            weather.httpLimitMs = 30000;
            boot();
        }

        // A fresh start: the sources count from now.
        void boot()
        {
            weather.startedAtMs = nowMs;
            weather.http.haveGood = false;
            weather.http.failure = Safety::Failure::None;
            park.nina = Nina::MountReading{};
        }

        void poll()
        {
            if (nowMs % 5000 == 0)
            {
                if (sqmeterUp)
                {
                    Safety::SqmeterReply r;
                    r.safe = sqmeterSafe;
                    r.reasonFlags = sqmeterSafe ? 0 : (1u << 4);
                    weather.http.recordGood(r, nowMs);
                }
                else
                    weather.http.recordFailure(Safety::Failure::Unreachable, "connection refused", nowMs);
            }
            if (nowMs % 2000 == 0)
            {
                if (ninaUp)
                {
                    park.nina = Nina::failedReading(Nina::ReadingStatus::Good, "", nowMs);
                    park.nina.connected = true;
                    park.nina.atPark = ninaParked;
                }
                else
                    park.nina = Nina::failedReading(Nina::ReadingStatus::Unreachable, "no reply", nowMs);
            }
        }

        void stepOnce()
        {
            poll();
            weather.nowMs = nowMs;
            park.nowMs = nowMs;
            const Safety::WeatherVerdict verdict = Safety::evaluateWeather(weather);
            const Safety::ParkResult result = Safety::evaluatePark(park);
            if (roof == State::Closing && nowMs - closingSinceMs >= 30000)
                roof = State::Closed;
            Inputs in;
            in.nowMs = nowMs;
            applySafety(in, verdict.verdict, result);
            in.roof = roof;
            in.moveTimeoutMs = 90000;
            WeatherHold::Step s;
            s.nowMs = nowMs;
            s.weatherSafe = in.weatherSafe;
            s.atOnce = skipsHold(verdict.reasons);
            in.weatherSafe = hold.update(s);
            note(seq.step(in), result);
        }

        void note(const Decision &given, const Safety::ParkResult &result)
        {
            Decision d = given;
            d.parkGap = parkGapOf(result); // as CloseOnUnsafe::report() does
            tally.requests += d.action == Action::RequestPark ? 1 : 0;
            tally.stops += d.action == Action::Stop ? 1 : 0;
            tally.alerts += d.alert ? 1 : 0;
            tally.started += d.event == Event::Started ? 1 : 0;
            tally.endedSafe = tally.endedSafe || d.event == Event::EndedSafe;
            tally.closed = tally.closed || d.event == Event::Closed;
            if (d.event == Event::ParkNotConfirmed)
            {
                tally.parkNotConfirmed++;
                Reasons reasons;
                reasons.park = parkReason(result, true);
                tally.texts.push_back(alertFor(d, reasonFor(d, reasons)));
            }
            if (d.action == Action::Close)
            {
                tally.closes++;
                seq.closeAnswered(CloseAnswer::Started);
                roof = State::Closing;
                closingSinceMs = nowMs;
            }
        }

        void run(uint64_t ms)
        {
            for (uint64_t t = 0; t < ms; t += STEP_MS)
            {
                nowMs += STEP_MS;
                stepOnce();
            }
        }

        // A reset as CloseOnUnsafe sees it: the sequence's saved record put
        // back into a new sequence, the hold fired when it was mid-attempt,
        // the clock and the sources starting again.
        void restart(uint64_t downMs)
        {
            const Sequence::Saved saved = seq.saved();
            seq = Sequence();
            hold = WeatherHold();
            seq.restore(saved);
            if (seq.phase() != Phase::Idle)
                hold.startFired();
            nowMs = BASE_MS + downMs;
            nowMs -= nowMs % STEP_MS;
            boot();
        }
    };

    // The access point goes down: SQMeter and N.I.N.A. both unreachable.
    void apDown(Site &s)
    {
        s.sqmeterUp = false;
        s.ninaUp = false;
    }

    void assertNinaTitle(const Tally &t)
    {
        TEST_ASSERT_TRUE(t.texts.size() > 0);
        for (const AlertText &text : t.texts)
        {
            TEST_ASSERT_NOT_NULL(strstr(text.title.c_str(), "Can't close: N.I.N.A. unreachable, mount park unknown"));
            TEST_ASSERT_NOT_NULL(strstr(text.message.c_str(), "N.I.N.A. not responding"));
        }
    }
} // namespace

// --- US4: the access point down, then up with the weather safe ---

void test_outage_keeps_asking_then_calls_off_when_safe(void)
{
    Site s;
    s.run(20000);
    apDown(s);
    s.run(85000); // stale after 30 s, the 60 s hold not yet reached
    TEST_ASSERT_EQUAL(0, s.tally.requests);
    s.run(10000);
    TEST_ASSERT_EQUAL(1, s.tally.requests);
    TEST_ASSERT_EQUAL(Phase::WaitPark, s.seq.phase());
    // An hour: attempts, park timeouts and back-offs, again and again.
    s.run(60 * MINUTE);
    TEST_ASSERT_EQUAL(0, s.tally.closes);
    TEST_ASSERT_EQUAL(0, s.tally.stops);
    TEST_ASSERT_TRUE(s.tally.started >= 6);
    TEST_ASSERT_TRUE(s.tally.parkNotConfirmed >= 6);
    TEST_ASSERT_TRUE(s.tally.requests >= 6 * 8);
    TEST_ASSERT_TRUE(s.tally.alerts >= 2); // the first, and again after the reminder interval
    assertNinaTitle(s.tally);
    TEST_ASSERT_TRUE(s.seq.phase() == Phase::WaitPark || s.seq.phase() == Phase::RetryWait);
    // The access point is back and the weather safe: N.I.N.A. still not parked.
    s.sqmeterUp = true;
    s.ninaUp = true;
    s.run(55000);
    TEST_ASSERT_FALSE(s.tally.endedSafe);
    s.run(10000);
    TEST_ASSERT_TRUE(s.tally.endedSafe);
    TEST_ASSERT_EQUAL(Phase::Idle, s.seq.phase());
    s.ninaParked = true;
    s.run(10 * MINUTE);
    TEST_ASSERT_EQUAL(0, s.tally.closes);
}

// --- the unsafe variant: N.I.N.A. comes back and parks ---

void test_outage_ends_in_park_and_close(void)
{
    Site s;
    s.sqmeterSafe = false; // SQMeter unsafe once it answers again
    s.run(20000);
    apDown(s);
    s.run(30 * MINUTE);
    TEST_ASSERT_EQUAL(0, s.tally.closes);
    s.sqmeterUp = true;
    s.ninaUp = true;
    s.ninaParked = true;
    s.run(15000);
    TEST_ASSERT_EQUAL(1, s.tally.closes);
    s.run(40000);
    TEST_ASSERT_TRUE(s.tally.closed);
    TEST_ASSERT_EQUAL(State::Closed, s.roof);
    TEST_ASSERT_EQUAL(Phase::Idle, s.seq.phase());
}

// --- a restart in the middle carries on ---

void test_a_restart_mid_outage_carries_on(void)
{
    for (const uint64_t into : {2 * MINUTE, 6 * MINUTE})
    {
        Site s;
        s.run(20000);
        apDown(s);
        s.run(100000 + into); // the hold fired, then WaitPark or RetryWait
        const Phase before = s.seq.phase();
        TEST_ASSERT_TRUE(before == Phase::WaitPark || before == Phase::RetryWait);
        s.restart(8000);
        TEST_ASSERT_TRUE(s.hold.fired());
        TEST_ASSERT_EQUAL(before, s.seq.phase());
        // The sources start again "waiting"; the attempt doesn't end safe.
        s.run(3 * MINUTE);
        TEST_ASSERT_FALSE(s.tally.endedSafe);
        TEST_ASSERT_EQUAL(0, s.tally.closes);
        // N.I.N.A. back and parked, SQMeter still down: the close goes ahead.
        s.ninaUp = true;
        s.ninaParked = true;
        s.run(6 * MINUTE);
        TEST_ASSERT_EQUAL(1, s.tally.closes);
        s.run(40000);
        TEST_ASSERT_TRUE(s.tally.closed);
    }
}

// --- FR-009 for each park source, N.I.N.A. unreachable in the rain ---

namespace
{
    // Rain at once, N.I.N.A. unreachable from the start.
    Site rainWithoutNina(uint8_t fitted, bool switchesOnly)
    {
        Site s;
        s.park.fittedSwitches = fitted;
        s.park.switchesOnly = switchesOnly;
        s.park.switch1 = Safety::SwitchReading::Closed;
        s.park.switch2 = Safety::SwitchReading::Closed;
        s.ninaUp = false;
        s.run(20000);
        Safety::SqmeterReply rain;
        rain.safe = false;
        rain.reasonFlags = Safety::SQMETER_RAIN;
        s.sqmeterSafe = false;
        s.weather.http.recordGood(rain, s.nowMs);
        return s;
    }

    void keepRaining(Site &s, uint64_t ms)
    {
        for (uint64_t t = 0; t < ms; t += 5000)
        {
            Safety::SqmeterReply rain;
            rain.safe = false;
            rain.reasonFlags = Safety::SQMETER_RAIN;
            s.sqmeterUp = false; // the poll would overwrite the reply: keep the rain fresh by hand
            s.weather.http.recordGood(rain, s.nowMs);
            s.run(5000);
        }
    }
} // namespace

void test_nina_only_never_closes_without_nina(void)
{
    Site s = rainWithoutNina(0, false);
    keepRaining(s, 60 * MINUTE);
    TEST_ASSERT_TRUE(s.tally.requests > 0);
    TEST_ASSERT_EQUAL(0, s.tally.closes);
    TEST_ASSERT_TRUE(s.tally.parkNotConfirmed >= 6);
    TEST_ASSERT_TRUE(s.tally.alerts >= 2);
    assertNinaTitle(s.tally);
}

void test_nina_and_switches_never_close_without_nina(void)
{
    Site s = rainWithoutNina(2, false);
    keepRaining(s, 30 * MINUTE);
    TEST_ASSERT_EQUAL(0, s.tally.closes);
    assertNinaTitle(s.tally);
}

void test_switches_only_close_without_nina(void)
{
    Site s = rainWithoutNina(1, true);
    keepRaining(s, 20000);
    TEST_ASSERT_EQUAL(1, s.tally.closes);
    TEST_ASSERT_EQUAL(0, s.tally.parkNotConfirmed);
}

void test_nina_and_switches_disagreeing_never_close(void)
{
    Site s = rainWithoutNina(1, false);
    s.ninaUp = true; // N.I.N.A. answers: not at park; switch 1 reads closed
    s.ninaParked = false;
    keepRaining(s, 30 * MINUTE);
    TEST_ASSERT_EQUAL(0, s.tally.closes);
    TEST_ASSERT_TRUE(s.tally.texts.size() > 0);
    for (const AlertText &text : s.tally.texts)
    {
        TEST_ASSERT_EQUAL_STRING("Can't close: N.I.N.A. and the park switches disagree", text.title.c_str());
        TEST_ASSERT_NOT_NULL(strstr(text.message.c_str(), "N.I.N.A. and the park switches disagree"));
    }
}

// --- the wording on its own ---

void test_park_gap_of_each_result(void)
{
    Safety::ParkResult r;
    r.reasons = Safety::ParkNinaUnreachable;
    TEST_ASSERT_EQUAL(ParkGap::NinaLost, parkGapOf(r));
    for (uint32_t bit : {Safety::ParkNinaStale, Safety::ParkNinaUnreadable, Safety::ParkNinaApiError, Safety::ParkNinaWaiting})
    {
        r.reasons = bit | Safety::ParkSwitch1Open;
        TEST_ASSERT_EQUAL(ParkGap::NinaLost, parkGapOf(r));
    }
    r.reasons = Safety::ParkSourcesDisagree | Safety::ParkMountNotAtPark;
    TEST_ASSERT_EQUAL(ParkGap::SourcesDisagree, parkGapOf(r));
    r.reasons = Safety::ParkMountNotAtPark;
    TEST_ASSERT_EQUAL(ParkGap::None, parkGapOf(r));
    r.reasons = Safety::ParkNinaUnreachable | Safety::ParkSwitch1Open;
    r.switchesOnly = true; // the switches alone: N.I.N.A. is never the missing input
    TEST_ASSERT_EQUAL(ParkGap::None, parkGapOf(r));
}

void test_park_alert_titles(void)
{
    Decision d;
    d.event = Event::ParkNotConfirmed;
    d.cause = Cause::ParkTimeout;
    d.limitMs = 240000;
    TEST_ASSERT_EQUAL_STRING("Mount didn't park", alertFor(d, "not parked: N.I.N.A. says mount not at park").title.c_str());
    d.parkGap = ParkGap::NinaLost;
    const AlertText lost = alertFor(d, "not parked: N.I.N.A. not responding: no reply");
    TEST_ASSERT_EQUAL_STRING("Can't close: N.I.N.A. unreachable, mount park unknown", lost.title.c_str());
    TEST_ASSERT_NOT_NULL(strstr(lost.message.c_str(), "within 240 s"));
    d.reminder = true;
    TEST_ASSERT_EQUAL_STRING("Still: Can't close: N.I.N.A. unreachable, mount park unknown", alertFor(d, "").title.c_str());
    d.reminder = false;
    d.parkGap = ParkGap::SourcesDisagree;
    TEST_ASSERT_EQUAL_STRING("Can't close: N.I.N.A. and the park switches disagree", alertFor(d, "").title.c_str());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_outage_keeps_asking_then_calls_off_when_safe);
    RUN_TEST(test_outage_ends_in_park_and_close);
    RUN_TEST(test_a_restart_mid_outage_carries_on);
    RUN_TEST(test_nina_only_never_closes_without_nina);
    RUN_TEST(test_nina_and_switches_never_close_without_nina);
    RUN_TEST(test_switches_only_close_without_nina);
    RUN_TEST(test_nina_and_switches_disagreeing_never_close);
    RUN_TEST(test_park_gap_of_each_result);
    RUN_TEST(test_park_alert_titles);
    return UNITY_END();
}
