// The weather verdict (lib/SafetyInputLogic): constitution III - any source
// unsafe, rain, a failed rain sensor, or a missing, stale or unreadable source
// makes the verdict unsafe; unknown is never safe. Spec 004 US1.

#include <string>
#include <unity.h>

#include "SafetyInput.h"
#include "SqmeterReply.h"
#include "fixtures.h"

using namespace SQM::Safety;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    constexpr uint32_t LIMIT_MS = 30000;
    constexpr uint32_t START_MS = 1000;
    // Well past the start-up window.
    constexpr uint32_t NOW_MS = 600000;

    SqmeterReply reply(bool safe, uint32_t flags, uint32_t evaluatedAgeMs = 0)
    {
        SqmeterReply r;
        r.safe = safe;
        r.reasonFlags = flags;
        r.evaluatedAgeMs = evaluatedAgeMs;
        return r;
    }

    SourceRecord goodRecord(bool safe, uint32_t flags, uint32_t atMs, Availability availability = Availability::Online)
    {
        SourceRecord record;
        record.enabled = true;
        record.availability = availability;
        record.recordGood(reply(safe, flags), atMs);
        return record;
    }

    // HTTP only, safe and fresh.
    WeatherInputs httpSafe()
    {
        WeatherInputs in;
        in.http = goodRecord(true, 0, NOW_MS - 1000, Availability::Unknown);
        in.httpLimitMs = LIMIT_MS;
        in.startedAtMs = START_MS;
        in.nowMs = NOW_MS;
        return in;
    }

    SourceStatus assessHttp(const SourceRecord &record, uint32_t nowMs)
    {
        return assessSource(record, SourceKind::SqmeterHttp, SourceClock{LIMIT_MS, START_MS, nowMs});
    }
} // namespace

// ---- Parsing SQMeter's replies ----

void test_parse_sqmeter_safe_example(void)
{
    SqmeterReply r;
    TEST_ASSERT_TRUE(parseSafetyJson(Fixtures::SAFETY_SAFE, r) == ReplyError::None);
    TEST_ASSERT_TRUE(r.safe);
    TEST_ASSERT_EQUAL_UINT32(0, r.reasonFlags);
    TEST_ASSERT_FALSE(r.unsafe());
}

void test_parse_sqmeter_rain_example(void)
{
    SqmeterReply r;
    TEST_ASSERT_TRUE(parseSafetyJson(Fixtures::SAFETY_RAIN, r) == ReplyError::None);
    TEST_ASSERT_FALSE(r.safe);
    TEST_ASSERT_EQUAL_UINT32(SQMETER_RAIN, r.reasonFlags);
    TEST_ASSERT_EQUAL_UINT32(412, r.evaluatedAgeMs);
    TEST_ASSERT_TRUE(r.unsafe());
}

void test_parse_without_age_is_good(void)
{
    SqmeterReply r;
    TEST_ASSERT_TRUE(parseSafetyJson(Fixtures::SAFETY_NO_AGE, r) == ReplyError::None);
    TEST_ASSERT_EQUAL_UINT32(0, r.evaluatedAgeMs);
}

void test_safe_with_flags_is_unsafe(void)
{
    SqmeterReply r;
    TEST_ASSERT_TRUE(parseSafetyJson(Fixtures::SAFETY_SAFE_WITH_FLAGS, r) == ReplyError::None);
    TEST_ASSERT_TRUE(r.safe);
    TEST_ASSERT_TRUE(r.unsafe());
}

void test_parse_rejects_broken_documents(void)
{
    struct Case
    {
        const char *body;
        ReplyError expected;
    };
    const Case cases[] = {
        {Fixtures::SAFETY_MISSING_SAFE, ReplyError::MissingSafe},
        {Fixtures::SAFETY_SAFE_AS_STRING, ReplyError::MissingSafe},
        {Fixtures::SAFETY_SAFE_NULL, ReplyError::MissingSafe},
        {Fixtures::SAFETY_MISSING_FLAGS, ReplyError::MissingReasonFlags},
        {Fixtures::SAFETY_NEGATIVE_FLAGS, ReplyError::MissingReasonFlags},
        {Fixtures::SAFETY_FRACTION_FLAGS, ReplyError::MissingReasonFlags},
        {Fixtures::SAFETY_BAD_AGE, ReplyError::BadEvaluatedAge},
        {Fixtures::SAFETY_ARRAY, ReplyError::NotObject},
        {Fixtures::SAFETY_TRUNCATED, ReplyError::NotJson},
        // ArduinoJson's filter skips what it can't read, so HTML comes out as "no object".
        {Fixtures::NOT_JSON, ReplyError::NotObject},
        {"", ReplyError::NotJson},
    };
    for (const Case &c : cases)
    {
        SqmeterReply r;
        r.safe = true;
        TEST_ASSERT_TRUE_MESSAGE(parseSafetyJson(c.body, r) == c.expected, c.body);
        TEST_ASSERT_TRUE_MESSAGE(r.safe, "a rejected reply leaves the output untouched");
    }
    TEST_ASSERT_EQUAL_STRING("missing reasonFlags", replyErrorText(ReplyError::MissingReasonFlags));
}

void test_parse_mqtt_payloads(void)
{
    TEST_ASSERT_TRUE(parseSafePayload("1").value());
    TEST_ASSERT_FALSE(parseSafePayload("0").value());
    TEST_ASSERT_FALSE(parseSafePayload("true").has_value());
    TEST_ASSERT_FALSE(parseSafePayload(" 1").has_value());
    TEST_ASSERT_FALSE(parseSafePayload("").has_value());
    TEST_ASSERT_TRUE(parseAvailability("online").value());
    TEST_ASSERT_FALSE(parseAvailability("offline").value());
    TEST_ASSERT_FALSE(parseAvailability("ONLINE").has_value());
}

// ---- One source ----

void test_disabled_source_is_off(void)
{
    SourceRecord record = goodRecord(true, 0, NOW_MS);
    record.enabled = false;
    TEST_ASSERT_TRUE(assessHttp(record, NOW_MS).state == SourceState::Off);
}

void test_fresh_at_exactly_the_limit(void)
{
    const SourceRecord record = goodRecord(true, 0, NOW_MS - LIMIT_MS);
    const SourceStatus s = assessHttp(record, NOW_MS);
    TEST_ASSERT_TRUE(s.state == SourceState::Good);
    TEST_ASSERT_EQUAL_UINT32(LIMIT_MS, s.ageMs);
}

void test_unsafe_when_http_stale_by_one_ms(void)
{
    const SourceRecord record = goodRecord(true, 0, NOW_MS - LIMIT_MS - 1);
    TEST_ASSERT_TRUE(assessHttp(record, NOW_MS).state == SourceState::Stale);
    WeatherInputs in = httpSafe();
    in.http = record;
    const WeatherVerdict v = evaluateWeather(in);
    TEST_ASSERT_TRUE(v.verdict == Verdict::Unsafe);
    TEST_ASSERT_TRUE(v.reasons & ReasonSourceStale);
}

void test_sqmeter_evaluated_age_counts_towards_stale(void)
{
    SourceRecord record;
    record.enabled = true;
    record.recordGood(reply(true, 0, 20000), NOW_MS - 10000);
    TEST_ASSERT_TRUE(assessHttp(record, NOW_MS).state == SourceState::Good);
    record.recordGood(reply(true, 0, 20001), NOW_MS - 10000);
    TEST_ASSERT_TRUE(assessHttp(record, NOW_MS).state == SourceState::Stale);
}

void test_one_failed_poll_inside_the_limit_keeps_last_good(void)
{
    SourceRecord record = goodRecord(true, 0, NOW_MS - 5000);
    record.recordFailure(Failure::Unreachable, "connection refused", NOW_MS - 100);
    TEST_ASSERT_TRUE(assessHttp(record, NOW_MS).state == SourceState::Good);
}

void test_unsafe_when_source_unreachable_past_the_limit(void)
{
    SourceRecord record = goodRecord(true, 0, NOW_MS - 40000);
    record.recordFailure(Failure::Unreachable, "connection refused", NOW_MS - 2000);
    const SourceStatus s = assessHttp(record, NOW_MS);
    TEST_ASSERT_TRUE(s.state == SourceState::Unreachable);
    TEST_ASSERT_EQUAL_STRING("connection refused", s.detail.c_str());
}

void test_unsafe_when_source_unreadable(void)
{
    WeatherInputs in = httpSafe();
    in.http.goodAtMs = NOW_MS - 31000;
    in.http.recordFailure(Failure::Unreadable, "missing reasonFlags", NOW_MS - 1000);
    const WeatherVerdict v = evaluateWeather(in);
    TEST_ASSERT_TRUE(v.verdict == Verdict::Unsafe);
    TEST_ASSERT_TRUE(v.reasons & ReasonSourceUnreadable);
    TEST_ASSERT_TRUE(v.http.state == SourceState::Unreadable);
}

void test_failure_older_than_last_good_is_just_stale(void)
{
    SourceRecord record = goodRecord(true, 0, NOW_MS - 40000);
    record.recordFailure(Failure::Unreadable, "not JSON", NOW_MS - 50000);
    TEST_ASSERT_TRUE(assessHttp(record, NOW_MS).state == SourceState::Stale);
}

void test_waiting_inside_the_start_window(void)
{
    SourceRecord record;
    record.enabled = true;
    TEST_ASSERT_TRUE(assessHttp(record, START_MS + LIMIT_MS).state == SourceState::Waiting);
    TEST_ASSERT_TRUE(assessHttp(record, START_MS + LIMIT_MS + 1).state == SourceState::Stale);
}

void test_unsafe_when_waiting_after_grace(void)
{
    WeatherInputs in = httpSafe();
    in.http = SourceRecord();
    in.http.enabled = true;
    in.http.recordFailure(Failure::Unreachable, "timeout", NOW_MS - 3000);
    const WeatherVerdict v = evaluateWeather(in);
    TEST_ASSERT_TRUE(v.verdict == Verdict::Unsafe);
    TEST_ASSERT_TRUE(v.reasons & ReasonSourceUnreachable);
}

void test_unknown_at_start_and_never_safe(void)
{
    WeatherInputs in = httpSafe();
    in.http = SourceRecord();
    in.http.enabled = true;
    in.nowMs = START_MS + 5000;
    const WeatherVerdict v = evaluateWeather(in);
    TEST_ASSERT_TRUE(v.verdict == Verdict::Unknown);
    TEST_ASSERT_FALSE(v.safe());
    TEST_ASSERT_EQUAL_STRING("unknown: SQMeter HTTP waiting for a first reply", describe(v).c_str());
}

void test_unsafe_beats_waiting_at_start(void)
{
    WeatherInputs in = httpSafe();
    in.nowMs = START_MS + 5000;
    in.http = goodRecord(false, 1, START_MS + 4000, Availability::Unknown);
    in.mqtt = SourceRecord();
    in.mqtt.enabled = true;
    const WeatherVerdict v = evaluateWeather(in);
    TEST_ASSERT_TRUE(v.verdict == Verdict::Unsafe);
    TEST_ASSERT_TRUE(v.mqtt.state == SourceState::Waiting);
}

void test_future_timestamp_counts_as_fresh(void)
{
    const SourceRecord record = goodRecord(true, 0, NOW_MS + 50);
    const SourceStatus s = assessHttp(record, NOW_MS);
    TEST_ASSERT_TRUE(s.state == SourceState::Good);
    TEST_ASSERT_EQUAL_UINT32(0, s.ageMs);
}

void test_millis_wrap_is_handled(void)
{
    const uint32_t before = UINT32_MAX - 999; // 1 s before the wrap
    SourceRecord record = goodRecord(true, 0, before);
    const SourceStatus s = assessSource(record, SourceKind::SqmeterHttp, SourceClock{LIMIT_MS, before - 100000, 4000});
    TEST_ASSERT_TRUE(s.state == SourceState::Good);
    TEST_ASSERT_EQUAL_UINT32(5000, s.ageMs);
}

// ---- MQTT availability ----

void test_mqtt_offline_is_unsafe_at_once(void)
{
    WeatherInputs in = httpSafe();
    in.mqtt = goodRecord(true, 0, NOW_MS - 1000, Availability::Offline);
    const WeatherVerdict v = evaluateWeather(in);
    TEST_ASSERT_TRUE(v.verdict == Verdict::Unsafe);
    TEST_ASSERT_TRUE(v.reasons & ReasonSourceOffline);
    TEST_ASSERT_EQUAL_STRING("unsafe: SQMeter MQTT offline", describe(v).c_str());
}

void test_mqtt_not_trusted_before_online(void)
{
    WeatherInputs in;
    in.mqtt = goodRecord(true, 0, NOW_MS - 1000, Availability::Unknown);
    in.startedAtMs = START_MS;
    in.nowMs = NOW_MS;
    const WeatherVerdict v = evaluateWeather(in);
    TEST_ASSERT_TRUE(v.mqtt.state == SourceState::Stale);
    TEST_ASSERT_TRUE(v.verdict == Verdict::Unsafe);
}

void test_mqtt_stale_limit_is_150_s(void)
{
    WeatherInputs in;
    in.mqtt = goodRecord(true, 0, NOW_MS - MQTT_STALE_MS);
    in.startedAtMs = START_MS;
    in.nowMs = NOW_MS;
    TEST_ASSERT_TRUE(evaluateWeather(in).verdict == Verdict::Safe);
    in.mqtt.goodAtMs = NOW_MS - MQTT_STALE_MS - 1;
    TEST_ASSERT_TRUE(evaluateWeather(in).verdict == Verdict::Unsafe);
}

// ---- Combination ----

void test_safe_when_every_source_safe(void)
{
    WeatherInputs in = httpSafe();
    in.mqtt = goodRecord(true, 0, NOW_MS - 2000);
    in.rainContact = RainContact::Dry;
    const WeatherVerdict v = evaluateWeather(in);
    TEST_ASSERT_TRUE(v.safe());
    TEST_ASSERT_EQUAL_UINT32(0, v.reasons);
    TEST_ASSERT_EQUAL_STRING("safe", describe(v).c_str());
}

void test_unsafe_when_any_source_unsafe_http(void)
{
    WeatherInputs in = httpSafe();
    in.http.recordGood(reply(false, 1u << 4), NOW_MS - 1000);
    in.mqtt = goodRecord(true, 0, NOW_MS - 2000);
    const WeatherVerdict v = evaluateWeather(in);
    TEST_ASSERT_TRUE(v.verdict == Verdict::Unsafe);
    TEST_ASSERT_EQUAL_UINT32(ReasonWeatherUnsafe, v.reasons);
    TEST_ASSERT_EQUAL_STRING("unsafe: SQMeter HTTP says unsafe: reasonFlags 0x10", describe(v).c_str());
}

void test_unsafe_when_any_source_unsafe_mqtt(void)
{
    WeatherInputs in = httpSafe();
    in.mqtt = goodRecord(false, 0, NOW_MS - 2000);
    const WeatherVerdict v = evaluateWeather(in);
    TEST_ASSERT_TRUE(v.verdict == Verdict::Unsafe);
    TEST_ASSERT_TRUE(v.reasons & ReasonWeatherUnsafe);
}

void test_safe_true_with_flags_is_unsafe_in_the_verdict(void)
{
    WeatherInputs in = httpSafe();
    in.http.recordGood(reply(true, 1u << 0), NOW_MS);
    TEST_ASSERT_TRUE(evaluateWeather(in).verdict == Verdict::Unsafe);
}

void test_rain_bit9_gives_rain(void)
{
    WeatherInputs in = httpSafe();
    in.http.recordGood(reply(false, SQMETER_RAIN), NOW_MS);
    const WeatherVerdict v = evaluateWeather(in);
    TEST_ASSERT_TRUE(v.verdict == Verdict::Unsafe);
    TEST_ASSERT_EQUAL_UINT32(ReasonRain, v.reasons);
    TEST_ASSERT_EQUAL_STRING("unsafe: SQMeter HTTP says unsafe: reasonFlags 0x200 (rain)", describe(v).c_str());
}

void test_rain_sensor_fault_bit10_unsafe(void)
{
    WeatherInputs in = httpSafe();
    in.mqtt = goodRecord(false, SQMETER_RAIN_SENSOR_FAULT, NOW_MS);
    const WeatherVerdict v = evaluateWeather(in);
    TEST_ASSERT_TRUE(v.verdict == Verdict::Unsafe);
    TEST_ASSERT_EQUAL_UINT32(ReasonRainSensorFault, v.reasons);
}

void test_rain_reason_kept_when_other_sources_stale(void)
{
    WeatherInputs in = httpSafe();
    in.http.recordGood(reply(false, SQMETER_RAIN), NOW_MS - 100000); // stale now
    in.mqtt = goodRecord(true, 0, NOW_MS - 200000);                  // stale too
    const WeatherVerdict v = evaluateWeather(in);
    TEST_ASSERT_TRUE(v.verdict == Verdict::Unsafe);
    TEST_ASSERT_TRUE(v.reasons & ReasonRain);
    TEST_ASSERT_TRUE(v.reasons & ReasonSourceStale);
}

void test_rain_contact_raining_is_unsafe(void)
{
    WeatherInputs in = httpSafe();
    in.rainContact = RainContact::Raining;
    const WeatherVerdict v = evaluateWeather(in);
    TEST_ASSERT_TRUE(v.verdict == Verdict::Unsafe);
    TEST_ASSERT_EQUAL_UINT32(ReasonRain, v.reasons);
    TEST_ASSERT_EQUAL_STRING("unsafe: Rain contact wet", describe(v).c_str());
}

void test_rain_contact_unreadable_is_unsafe(void)
{
    WeatherInputs in = httpSafe();
    in.rainContact = RainContact::Unreadable;
    const WeatherVerdict v = evaluateWeather(in);
    TEST_ASSERT_TRUE(v.verdict == Verdict::Unsafe);
    TEST_ASSERT_EQUAL_UINT32(ReasonRainContactUnreadable, v.reasons);
}

void test_rain_contact_alone_is_no_weather_source(void)
{
    WeatherInputs in;
    in.rainContact = RainContact::Dry;
    in.nowMs = NOW_MS;
    const WeatherVerdict v = evaluateWeather(in);
    TEST_ASSERT_TRUE(v.verdict == Verdict::Unsafe);
    TEST_ASSERT_EQUAL_UINT32(ReasonNoWeatherSource, v.reasons);
}

void test_no_source_configured_is_unsafe(void)
{
    WeatherInputs in;
    in.nowMs = START_MS; // even at start: nothing will ever answer
    in.startedAtMs = START_MS;
    const WeatherVerdict v = evaluateWeather(in);
    TEST_ASSERT_TRUE(v.verdict == Verdict::Unsafe);
    TEST_ASSERT_EQUAL_STRING("unsafe: No weather source: SQMeter HTTP and MQTT are both off", describe(v).c_str());
}

// ---- Text: value and limit (constitution III) ----

void test_describe_stale_states_age_and_limit(void)
{
    WeatherInputs in = httpSafe();
    in.http.goodAtMs = NOW_MS - 75000;
    TEST_ASSERT_EQUAL_STRING("unsafe: SQMeter HTTP stale: no good reply for 75 s > 30 s", describe(evaluateWeather(in)).c_str());
    in.http.recordFailure(Failure::Unreachable, "connection refused", NOW_MS - 500);
    TEST_ASSERT_EQUAL_STRING(
        "unsafe: SQMeter HTTP unreachable: connection refused (no good reply for 75 s > 30 s)", describe(evaluateWeather(in)).c_str());
}

void test_format_seconds(void)
{
    TEST_ASSERT_EQUAL_STRING("30 s", formatSeconds(30000).c_str());
    TEST_ASSERT_EQUAL_STRING("30.0 s", formatSeconds(30001).c_str());
    TEST_ASSERT_EQUAL_STRING("0.4 s", formatSeconds(412).c_str());
}

namespace
{
    // One SQMeter source for the sweep: 0 off, 1 good-safe, 2 good-unsafe,
    // 3 rain, 4 rain sensor fault, 5 stale, 6 unreachable, 7 unreadable,
    // 8 offline (MQTT), 9 enabled with nothing yet.
    constexpr int SWEEP_KINDS = 10;

    SourceRecord sweepRecord(int kind, bool mqtt, uint32_t nowMs)
    {
        SourceRecord r;
        r.enabled = kind != 0;
        r.availability = mqtt ? Availability::Online : Availability::Unknown;
        const uint32_t flags[] = {0, 0, 1u << 4, SQMETER_RAIN, SQMETER_RAIN_SENSOR_FAULT};
        if (kind >= 1 && kind <= 4)
            r.recordGood(reply(kind == 1, flags[kind]), nowMs);
        if (kind == 5)
            r.recordGood(reply(true, 0), nowMs - 200000);
        if (kind == 6)
            r.recordFailure(Failure::Unreachable, "timeout", nowMs);
        if (kind == 7)
            r.recordFailure(Failure::Unreadable, "not JSON", nowMs);
        if (kind == 8)
        {
            r.recordGood(reply(true, 0), nowMs);
            r.availability = Availability::Offline;
        }
        return r;
    }

    // HTTP has no availability, so "offline" (8) leaves it good.
    bool sweepOk(int kind, bool mqtt)
    {
        return kind == 0 || kind == 1 || (kind == 8 && !mqtt);
    }
} // namespace

// ---- SC-004: never safe while any enabled source is bad ----

void test_never_safe_with_a_bad_source_exhaustive(void)
{
    const RainContact contacts[] = {RainContact::NotFitted, RainContact::Dry, RainContact::Raining, RainContact::Unreadable};
    for (int h = 0; h < SWEEP_KINDS; ++h)
        for (int m = 0; m < SWEEP_KINDS; ++m)
            for (RainContact contact : contacts)
                for (uint32_t now : {START_MS + 1000, NOW_MS})
                {
                    WeatherInputs in;
                    in.http = sweepRecord(h, false, now);
                    in.mqtt = sweepRecord(m, true, now);
                    in.rainContact = contact;
                    in.startedAtMs = START_MS;
                    in.nowMs = now;
                    const bool contactOk = contact == RainContact::NotFitted || contact == RainContact::Dry;
                    const bool expectSafe = sweepOk(h, false) && sweepOk(m, true) && contactOk && (h != 0 || m != 0);
                    const WeatherVerdict v = evaluateWeather(in);
                    TEST_ASSERT_EQUAL_MESSAGE(expectSafe, v.safe(), describe(v).c_str());
                    if (!v.safe())
                        TEST_ASSERT_TRUE(v.verdict == Verdict::Unknown || v.reasons != 0);
                }
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_sqmeter_safe_example);
    RUN_TEST(test_parse_sqmeter_rain_example);
    RUN_TEST(test_parse_without_age_is_good);
    RUN_TEST(test_safe_with_flags_is_unsafe);
    RUN_TEST(test_parse_rejects_broken_documents);
    RUN_TEST(test_parse_mqtt_payloads);
    RUN_TEST(test_disabled_source_is_off);
    RUN_TEST(test_fresh_at_exactly_the_limit);
    RUN_TEST(test_unsafe_when_http_stale_by_one_ms);
    RUN_TEST(test_sqmeter_evaluated_age_counts_towards_stale);
    RUN_TEST(test_one_failed_poll_inside_the_limit_keeps_last_good);
    RUN_TEST(test_unsafe_when_source_unreachable_past_the_limit);
    RUN_TEST(test_unsafe_when_source_unreadable);
    RUN_TEST(test_failure_older_than_last_good_is_just_stale);
    RUN_TEST(test_waiting_inside_the_start_window);
    RUN_TEST(test_unsafe_when_waiting_after_grace);
    RUN_TEST(test_unknown_at_start_and_never_safe);
    RUN_TEST(test_unsafe_beats_waiting_at_start);
    RUN_TEST(test_future_timestamp_counts_as_fresh);
    RUN_TEST(test_millis_wrap_is_handled);
    RUN_TEST(test_mqtt_offline_is_unsafe_at_once);
    RUN_TEST(test_mqtt_not_trusted_before_online);
    RUN_TEST(test_mqtt_stale_limit_is_150_s);
    RUN_TEST(test_safe_when_every_source_safe);
    RUN_TEST(test_unsafe_when_any_source_unsafe_http);
    RUN_TEST(test_unsafe_when_any_source_unsafe_mqtt);
    RUN_TEST(test_safe_true_with_flags_is_unsafe_in_the_verdict);
    RUN_TEST(test_rain_bit9_gives_rain);
    RUN_TEST(test_rain_sensor_fault_bit10_unsafe);
    RUN_TEST(test_rain_reason_kept_when_other_sources_stale);
    RUN_TEST(test_rain_contact_raining_is_unsafe);
    RUN_TEST(test_rain_contact_unreadable_is_unsafe);
    RUN_TEST(test_rain_contact_alone_is_no_weather_source);
    RUN_TEST(test_no_source_configured_is_unsafe);
    RUN_TEST(test_describe_stale_states_age_and_limit);
    RUN_TEST(test_format_seconds);
    RUN_TEST(test_never_safe_with_a_bad_source_exhaustive);
    return UNITY_END();
}
