// What each SQMeter answer does to its source (lib/SafetyInputLogic
// SqmeterSource): spec 004 FR-010, FR-011 and the retained-copy rule.
// Constitution III: nothing unreadable or replayed ever makes a source safe.

#include <unity.h>

#include <string>

#include "SafetyInput.h"
#include "SqmeterSource.h"

using namespace SQM::Safety;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    constexpr TimeMs NOW_MS = 1000000;
    const MqttTopics TOPICS = MqttTopics::under("sqmeter");
    const char *const SAFE_JSON = R"({"safe":true,"reasonFlags":0,"evaluatedAgeMs":5})";
    const char *const RAIN_JSON = R"({"safe":false,"reasonFlags":512,"evaluatedAgeMs":5})";

    // A record that has seen its retained copies, so live messages count.
    SourceRecord liveRecord()
    {
        SourceRecord r;
        r.enabled = true;
        r.safeSeen = true;
        r.safetySeen = true;
        return r;
    }
} // namespace

void test_topics_under_base(void)
{
    TEST_ASSERT_EQUAL_STRING("sqmeter/availability", TOPICS.availability.c_str());
    TEST_ASSERT_EQUAL_STRING("sqmeter/safe", TOPICS.safe.c_str());
    TEST_ASSERT_EQUAL_STRING("sqmeter/safety", TOPICS.safety.c_str());
}

void test_http_no_reply_is_unreachable(void)
{
    SourceRecord r = liveRecord();
    recordHttpReply(r, HttpOutcome{-1, "", "connection refused"}, NOW_MS);
    TEST_ASSERT_FALSE(r.haveGood);
    TEST_ASSERT_TRUE(r.failure == Failure::Unreachable);
    TEST_ASSERT_EQUAL_STRING("connection refused", r.detail.c_str());
}

void test_http_non_200_is_unreadable(void)
{
    SourceRecord r = liveRecord();
    recordHttpReply(r, HttpOutcome{404, SAFE_JSON, ""}, NOW_MS);
    TEST_ASSERT_FALSE(r.haveGood);
    TEST_ASSERT_TRUE(r.failure == Failure::Unreadable);
    TEST_ASSERT_EQUAL_STRING("HTTP 404", r.detail.c_str());
}

void test_http_bad_document_is_unreadable(void)
{
    SourceRecord r = liveRecord();
    recordHttpReply(r, HttpOutcome{200, R"({"safe":true})", ""}, NOW_MS);
    TEST_ASSERT_FALSE(r.haveGood);
    TEST_ASSERT_EQUAL_STRING("missing reasonFlags", r.detail.c_str());
}

void test_http_good_reply_is_recorded(void)
{
    SourceRecord r = liveRecord();
    recordHttpReply(r, HttpOutcome{200, RAIN_JSON, ""}, NOW_MS);
    TEST_ASSERT_TRUE(r.haveGood);
    TEST_ASSERT_EQUAL_UINT64(NOW_MS, r.goodAtMs);
    TEST_ASSERT_EQUAL_UINT32(512, r.last.reasonFlags);
}

void test_availability_sets_and_rejects(void)
{
    SourceRecord r = liveRecord();
    applyMqttMessage(r, TOPICS, "sqmeter/availability", "online", NOW_MS);
    TEST_ASSERT_TRUE(r.availability == Availability::Online);
    applyMqttMessage(r, TOPICS, "sqmeter/availability", "offline", NOW_MS);
    TEST_ASSERT_TRUE(r.availability == Availability::Offline);
    applyMqttMessage(r, TOPICS, "sqmeter/availability", "gone", NOW_MS);
    TEST_ASSERT_TRUE(r.availability == Availability::Offline);
    TEST_ASSERT_TRUE(r.failure == Failure::Unreadable);
}

void test_rain_json_then_safe_1_keeps_rain(void)
{
    SourceRecord r = liveRecord();
    applyMqttMessage(r, TOPICS, "sqmeter/safety", RAIN_JSON, NOW_MS);
    applyMqttMessage(r, TOPICS, "sqmeter/safe", "1", NOW_MS + 10);
    TEST_ASSERT_TRUE(r.haveGood);
    TEST_ASSERT_EQUAL_UINT32(512, r.last.reasonFlags);
    TEST_ASSERT_TRUE(r.last.unsafe());
}

void test_safe_0_is_unsafe(void)
{
    SourceRecord r = liveRecord();
    applyMqttMessage(r, TOPICS, "sqmeter/safe", "0", NOW_MS);
    TEST_ASSERT_TRUE(r.haveGood);
    TEST_ASSERT_TRUE(r.last.unsafe());
}

void test_bad_payloads_are_unreadable_and_never_good(void)
{
    SourceRecord r = liveRecord();
    applyMqttMessage(r, TOPICS, "sqmeter/safe", "true", NOW_MS);
    TEST_ASSERT_FALSE(r.haveGood);
    TEST_ASSERT_EQUAL_STRING("safe not 1/0", r.detail.c_str());
    applyMqttMessage(r, TOPICS, "sqmeter/safety", "{}", NOW_MS);
    TEST_ASSERT_FALSE(r.haveGood);
    TEST_ASSERT_EQUAL_STRING("safety missing safe", r.detail.c_str());
}

void test_other_topics_are_ignored(void)
{
    SourceRecord r = liveRecord();
    applyMqttMessage(r, TOPICS, "sqmeter/safe/extra", "1", NOW_MS);
    applyMqttMessage(r, TOPICS, "other/safe", "1", NOW_MS);
    TEST_ASSERT_FALSE(r.haveGood);
    TEST_ASSERT_TRUE(r.failure == Failure::None);
}

void test_retained_safe_copy_never_counts(void)
{
    SourceRecord r;
    r.enabled = true;
    r.resubscribed();
    applyMqttMessage(r, TOPICS, "sqmeter/safety", SAFE_JSON, NOW_MS);
    applyMqttMessage(r, TOPICS, "sqmeter/safe", "1", NOW_MS);
    TEST_ASSERT_FALSE(r.haveGood);
    // The next (live) messages count.
    applyMqttMessage(r, TOPICS, "sqmeter/safety", SAFE_JSON, NOW_MS + 60000);
    TEST_ASSERT_TRUE(r.haveGood);
    TEST_ASSERT_EQUAL_UINT64(NOW_MS + 60000, r.goodAtMs);
}

void test_retained_unsafe_copy_counts(void)
{
    SourceRecord r;
    r.enabled = true;
    r.resubscribed();
    applyMqttMessage(r, TOPICS, "sqmeter/safety", RAIN_JSON, NOW_MS);
    TEST_ASSERT_TRUE(r.haveGood);
    TEST_ASSERT_TRUE(r.last.unsafe());
}

void test_resubscribe_after_live_messages_skips_the_replay(void)
{
    SourceRecord r = liveRecord();
    applyMqttMessage(r, TOPICS, "sqmeter/safe", "1", NOW_MS);
    r.resubscribed();
    applyMqttMessage(r, TOPICS, "sqmeter/safe", "1", NOW_MS + 500000);
    TEST_ASSERT_EQUAL_UINT64(NOW_MS, r.goodAtMs);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_topics_under_base);
    RUN_TEST(test_http_no_reply_is_unreachable);
    RUN_TEST(test_http_non_200_is_unreadable);
    RUN_TEST(test_http_bad_document_is_unreadable);
    RUN_TEST(test_http_good_reply_is_recorded);
    RUN_TEST(test_availability_sets_and_rejects);
    RUN_TEST(test_rain_json_then_safe_1_keeps_rain);
    RUN_TEST(test_safe_0_is_unsafe);
    RUN_TEST(test_bad_payloads_are_unreadable_and_never_good);
    RUN_TEST(test_other_topics_are_ignored);
    RUN_TEST(test_retained_safe_copy_never_counts);
    RUN_TEST(test_retained_unsafe_copy_counts);
    RUN_TEST(test_resubscribe_after_live_messages_skips_the_replay);
    return UNITY_END();
}
