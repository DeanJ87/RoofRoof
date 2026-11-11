#include <unity.h>

#include <ArduinoJson.h>

#include <string>

#include "InputsReport.h"

using namespace SQM;
using namespace SQM::Safety;

// Spec 015 FR-016, FR-017: GET /api/roof/inputs, the readings the device
// already holds, written with the device's own names and texts.

namespace
{
    DynamicJsonDocument write(const InputsReport &report)
    {
        DynamicJsonDocument doc(INPUTS_REPORT_CAPACITY);
        writeInputsReport(doc.to<JsonObject>(), report);
        TEST_ASSERT_FALSE_MESSAGE(doc.overflowed(), "the report doesn't fit its capacity");
        return doc;
    }

    SourceStatus source(SourceKind kind, SourceState state, const std::string &detail = "")
    {
        SourceStatus s;
        s.kind = kind;
        s.state = state;
        s.ageMs = 2100;
        s.limitMs = 30000;
        s.detail = detail;
        return s;
    }

    InputsReport safeAndParked()
    {
        InputsReport r;
        r.weather.verdict = Verdict::Safe;
        r.weather.http = source(SourceKind::SqmeterHttp, SourceState::Good);
        r.weather.http.sqmeterSafe = true;
        r.weather.mqtt = source(SourceKind::SqmeterMqtt, SourceState::Off);
        r.weather.rainContact = RainContact::NotFitted;
        r.park.parked = true;
        r.nina.status = Nina::ReadingStatus::Good;
        r.nina.connected = true;
        r.nina.atPark = true;
        r.nina.takenAtMs = 10000;
        r.nowMs = 10900;
        r.httpGeneration = 2;
        r.mqttGeneration = 1;
        r.ninaGeneration = 3;
        return r;
    }
} // namespace

void setUp() {}
void tearDown() {}

void test_safe_and_parked()
{
    const InputsReport r = safeAndParked();
    DynamicJsonDocument doc = write(r);
    TEST_ASSERT_EQUAL_STRING("safe", doc["weather"]["verdict"]);
    TEST_ASSERT_EQUAL_STRING(describe(r.weather).c_str(), doc["weather"]["text"]);
    TEST_ASSERT_TRUE(doc["weather"]["http"]["enabled"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("good", doc["weather"]["http"]["state"]);
    TEST_ASSERT_EQUAL_UINT32(2, doc["weather"]["http"]["generation"]);
    TEST_ASSERT_EQUAL_UINT32(2100, doc["weather"]["http"]["ageMs"]);
    TEST_ASSERT_EQUAL_UINT32(30000, doc["weather"]["http"]["limitMs"]);
    TEST_ASSERT_TRUE(doc["weather"]["http"]["safe"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("", doc["weather"]["http"]["text"]);
    TEST_ASSERT_FALSE(doc["weather"]["mqtt"]["enabled"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("off", doc["weather"]["mqtt"]["state"]);
    TEST_ASSERT_EQUAL_UINT32(1, doc["weather"]["mqtt"]["generation"]);
    TEST_ASSERT_EQUAL_STRING("notFitted", doc["weather"]["rainContact"]);
    TEST_ASSERT_TRUE(doc["park"]["parked"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("parked", doc["park"]["text"]);
    TEST_ASSERT_EQUAL_STRING("good", doc["nina"]["state"]);
    TEST_ASSERT_EQUAL_UINT32(3, doc["nina"]["generation"]);
    TEST_ASSERT_TRUE(doc["nina"]["connected"].as<bool>());
    TEST_ASSERT_TRUE(doc["nina"]["atPark"].as<bool>());
    TEST_ASSERT_FALSE(doc["nina"]["slewing"].as<bool>());
    TEST_ASSERT_EQUAL_UINT32(900, doc["nina"]["ageMs"]);
    TEST_ASSERT_EQUAL_STRING("", doc["nina"]["text"]);
}

void test_every_source_state_has_its_name()
{
    const std::pair<SourceState, const char *> names[] = {
        {SourceState::Off, "off"},
        {SourceState::Waiting, "waiting"},
        {SourceState::Good, "good"},
        {SourceState::Stale, "stale"},
        {SourceState::Unreachable, "unreachable"},
        {SourceState::Unreadable, "unreadable"},
        {SourceState::Offline, "offline"},
    };
    for (const auto &[state, name] : names)
    {
        InputsReport r = safeAndParked();
        r.weather.mqtt = source(SourceKind::SqmeterMqtt, state, "connection refused");
        DynamicJsonDocument doc = write(r);
        TEST_ASSERT_EQUAL_STRING(name, doc["weather"]["mqtt"]["state"]);
        TEST_ASSERT_EQUAL(state != SourceState::Off, doc["weather"]["mqtt"]["enabled"].as<bool>());
        TEST_ASSERT_EQUAL_STRING(describeSource(r.weather.mqtt).c_str(), doc["weather"]["mqtt"]["text"]);
    }
}

void test_unsafe_names_the_reason()
{
    InputsReport r = safeAndParked();
    r.weather.verdict = Verdict::Unsafe;
    r.weather.http = source(SourceKind::SqmeterHttp, SourceState::Unreachable, "connection refused");
    r.weather.reasons = ReasonSourceUnreachable;
    DynamicJsonDocument doc = write(r);
    TEST_ASSERT_EQUAL_STRING("unsafe", doc["weather"]["verdict"]);
    const std::string text = doc["weather"]["http"]["text"].as<std::string>();
    TEST_ASSERT_TRUE_MESSAGE(text.find("connection refused") != std::string::npos, text.c_str());
    TEST_ASSERT_EQUAL_STRING(describe(r.weather).c_str(), doc["weather"]["text"]);
    r.weather.verdict = Verdict::Unknown;
    TEST_ASSERT_EQUAL_STRING("unknown", write(r)["weather"]["verdict"]);
}

void test_every_rain_contact_has_its_name()
{
    const std::pair<RainContact, const char *> names[] = {
        {RainContact::NotFitted, "notFitted"},
        {RainContact::Dry, "dry"},
        {RainContact::Raining, "raining"},
        {RainContact::Unreadable, "unreadable"},
    };
    for (const auto &[contact, name] : names)
    {
        InputsReport r = safeAndParked();
        r.weather.rainContact = contact;
        TEST_ASSERT_EQUAL_STRING(name, write(r)["weather"]["rainContact"]);
    }
}

void test_every_nina_state_has_its_name_and_flags_only_when_good()
{
    const std::pair<Nina::ReadingStatus, const char *> names[] = {
        {Nina::ReadingStatus::NotConfigured, "notConfigured"},
        {Nina::ReadingStatus::Waiting, "waiting"},
        {Nina::ReadingStatus::Good, "good"},
        {Nina::ReadingStatus::Unreachable, "unreachable"},
        {Nina::ReadingStatus::Unreadable, "unreadable"},
        {Nina::ReadingStatus::ApiError, "apiError"},
    };
    for (const auto &[status, name] : names)
    {
        InputsReport r = safeAndParked();
        r.nina.status = status;
        r.nina.slewing = true;
        r.nina.detail = "HTTP 500";
        DynamicJsonDocument doc = write(r);
        const bool good = status == Nina::ReadingStatus::Good;
        TEST_ASSERT_EQUAL_STRING(name, doc["nina"]["state"]);
        TEST_ASSERT_EQUAL(good, doc["nina"]["connected"].as<bool>());
        TEST_ASSERT_EQUAL(good, doc["nina"]["atPark"].as<bool>());
        TEST_ASSERT_EQUAL(good, doc["nina"]["slewing"].as<bool>());
        TEST_ASSERT_EQUAL_STRING(good ? "" : "HTTP 500", doc["nina"]["text"]);
    }
}

void test_the_nina_age_never_goes_negative()
{
    InputsReport r = safeAndParked();
    r.nina.takenAtMs = r.nowMs + 50; // taken after the report's clock was read
    TEST_ASSERT_EQUAL_UINT32(0, write(r)["nina"]["ageMs"]);
    r.nina.takenAtMs = 0;
    r.nowMs = 0xFFFFFFFFFFULL;
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, write(r)["nina"]["ageMs"]);
}

void test_the_park_text_is_the_park_rule_s()
{
    InputsReport r = safeAndParked();
    r.park.parked = false;
    r.park.reasons = ParkMountNotAtPark | ParkSwitch1Open;
    DynamicJsonDocument doc = write(r);
    TEST_ASSERT_FALSE(doc["park"]["parked"].as<bool>());
    TEST_ASSERT_EQUAL_STRING(describe(r.park).c_str(), doc["park"]["text"]);
}

// Readings only: no field carries an address or a topic, and the longest
// texts still fit.
void test_no_address_or_topic_and_long_texts_fit()
{
    InputsReport r = safeAndParked();
    const std::string longText(2000, 'x');
    r.weather.verdict = Verdict::Unsafe;
    r.weather.reasons = ReasonSourceUnreadable | ReasonSourceStale | ReasonRainContactUnreadable;
    r.weather.http = source(SourceKind::SqmeterHttp, SourceState::Unreadable, longText);
    r.weather.mqtt = source(SourceKind::SqmeterMqtt, SourceState::Stale, longText);
    r.weather.rainContact = RainContact::Unreadable;
    r.park.parked = false;
    r.park.reasons = 0x3FFF;
    r.park.ninaDetail = longText;
    r.nina.status = Nina::ReadingStatus::ApiError;
    r.nina.detail = longText;
    DynamicJsonDocument doc = write(r);
    std::string json;
    serializeJson(doc, json);
    TEST_ASSERT_TRUE(json.find("http://") == std::string::npos);
    TEST_ASSERT_TRUE(json.find("url") == std::string::npos);
    TEST_ASSERT_TRUE(json.find("topic") == std::string::npos);
    TEST_ASSERT_TRUE(json.find("password") == std::string::npos);
    TEST_ASSERT_TRUE(doc["nina"]["text"].as<std::string>().size() <= 243);
    TEST_ASSERT_TRUE(doc["weather"]["text"].as<std::string>().size() <= 243);
}

// A clipped text never ends inside a UTF-8 character.
void test_clipped_text_keeps_whole_characters()
{
    InputsReport r = safeAndParked();
    r.nina.status = Nina::ReadingStatus::ApiError;
    r.nina.detail = std::string(239, 'x') + "\xC2\xB0\xC2\xB0"; // "°°" across the cut
    const std::string text = write(r)["nina"]["text"].as<std::string>();
    TEST_ASSERT_EQUAL_STRING((std::string(239, 'x') + "...").c_str(), text.c_str());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_safe_and_parked);
    RUN_TEST(test_every_source_state_has_its_name);
    RUN_TEST(test_unsafe_names_the_reason);
    RUN_TEST(test_every_rain_contact_has_its_name);
    RUN_TEST(test_every_nina_state_has_its_name_and_flags_only_when_good);
    RUN_TEST(test_the_nina_age_never_goes_negative);
    RUN_TEST(test_the_park_text_is_the_park_rule_s);
    RUN_TEST(test_no_address_or_topic_and_long_texts_fit);
    RUN_TEST(test_clipped_text_keeps_whole_characters);
    return UNITY_END();
}
