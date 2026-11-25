// Spec 030 (FR-011, FR-012): SQMeter's reasons as words, never a code. The
// reply's "reasons" text is read, cleaned and capped; without it the flags
// read as words; and an imaging app's OpenShutter refused for unsafe weather
// names the reason in those words.

#include <cstring>
#include <string>
#include <unity.h>

#include "CommandCheck.h"
#include "Config.h" // RoofIoLogic builds lib/ConfigModel (SettingsGuard); name it so the native build finds its dependencies
#include "DomeBridge.h"
#include "RoofController.h"
#include "SafetyInput.h"
#include "SqmeterReply.h"

#include "../roof_sim/RoofSim.h"

using namespace SQM;
using namespace SQM::Safety;
using Roof::Command;
using Roof::Origin;
using RoofTest::Bench;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    constexpr uint32_t NOW_MS = 600000;
    Origin remote = Origin::Client;

    SqmeterReply reply(bool safe, uint32_t flags)
    {
        SqmeterReply r;
        r.safe = safe;
        r.reasonFlags = flags;
        return r;
    }

    WeatherInputs httpSafe()
    {
        WeatherInputs in;
        in.http.enabled = true;
        in.http.recordGood(reply(true, 0), NOW_MS - 1000);
        in.httpLimitMs = 30000;
        in.startedAtMs = 0;
        in.nowMs = NOW_MS;
        return in;
    }

    RoofIo::GateInputs safeAndParked()
    {
        RoofIo::GateInputs g;
        g.weather = Verdict::Safe;
        g.weatherText = "safe";
        g.parked = true;
        g.parkText = "parked";
        return g;
    }

    RoofIo::CommandContext ctx()
    {
        return RoofIo::CommandContext{};
    }

    bool run(Bench &b, Command command, Origin origin, const RoofIo::GateInputs &gates)
    {
        const RoofIo::CommandCheck check = RoofIo::checkCommand(command, origin, gates, b.roof, ctx());
        return check.accepted && b.command(command, origin).accepted;
    }

    void expectStill(Bench &b)
    {
        for (int i = 0; i < 200; ++i)
            TEST_ASSERT_TRUE(b.step().command == Driver::Command::Off);
    }
} // namespace

void test_reasons_are_read_joined_and_kept(void)
{
    SqmeterReply r;
    TEST_ASSERT_TRUE(
        parseSafetyJson(R"({"safe":false,"reasonFlags":16,"reasons":["Cloud 100% >= 30%","Humidity 97% >= 95%"]})", r) == ReplyError::None);
    TEST_ASSERT_EQUAL_STRING("Cloud 100% >= 30%; Humidity 97% >= 95%", r.reasons.c_str());
    TEST_ASSERT_EQUAL_UINT32(16, r.reasonFlags);
    // Without the array, or with something else in its place: no text, the reply still good.
    SqmeterReply none;
    TEST_ASSERT_TRUE(parseSafetyJson(R"({"safe":false,"reasonFlags":16})", none) == ReplyError::None);
    TEST_ASSERT_EQUAL_STRING("", none.reasons.c_str());
    TEST_ASSERT_TRUE(parseSafetyJson(R"({"safe":false,"reasonFlags":16,"reasons":"cloud"})", none) == ReplyError::None);
    TEST_ASSERT_EQUAL_STRING("", none.reasons.c_str());
    TEST_ASSERT_TRUE(parseSafetyJson(R"({"safe":false,"reasonFlags":16,"reasons":[1,"Cloud",null]})", none) == ReplyError::None);
    TEST_ASSERT_EQUAL_STRING("Cloud", none.reasons.c_str());
}

void test_reasons_drop_control_characters_and_are_capped(void)
{
    SqmeterReply r;
    TEST_ASSERT_TRUE(parseSafetyJson(R"({"safe":false,"reasonFlags":16,"reasons":["Cloud\n100%\t\u0007"]})", r) == ReplyError::None);
    TEST_ASSERT_EQUAL_STRING("Cloud100%", r.reasons.c_str());
    // A long array: the reply is still read; the text is cut at 160 bytes.
    std::string body = R"({"safe":false,"reasonFlags":16,"reasons":[)";
    for (int i = 0; i < 40; ++i)
        body += std::string(i ? "," : "") + "\"Reason number " + std::to_string(i) + " is a long one\"";
    body += "]}";
    SqmeterReply many;
    TEST_ASSERT_TRUE(parseSafetyJson(body, many) == ReplyError::None);
    TEST_ASSERT_EQUAL_UINT32(16, many.reasonFlags);
    TEST_ASSERT_EQUAL_UINT32(MAX_REASONS_TEXT, many.reasons.size());
    TEST_ASSERT_EQUAL_STRING_LEN("Reason number 0 is a long one; Reason number 1", many.reasons.c_str(), 46);
}

void test_reasons_never_cut_a_utf8_character(void)
{
    // "°" is two bytes: 159 ASCII bytes and then it would end at byte 161.
    const std::string text = std::string(159, 'a') + "\xC2\xB0" + "C";
    const std::string kept = cleanReasonText(text);
    TEST_ASSERT_EQUAL_UINT32(159, kept.size());
    TEST_ASSERT_EQUAL_STRING(
        "ab",
        cleanReasonText("a\x01"
                        "b\x7F")
            .c_str());
    TEST_ASSERT_EQUAL_STRING("abc", cleanReasonText("abcdef", 3).c_str());
}

void test_flags_read_as_words(void)
{
    TEST_ASSERT_EQUAL_STRING("cloud cover", reasonFlagsText(1u << 4).c_str());
    TEST_ASSERT_EQUAL_STRING("manual override, rain, external hold", reasonFlagsText((1u << 0) | (1u << 9) | (1u << 17)).c_str());
    TEST_ASSERT_EQUAL_STRING("other reason (bit 18), other reason (bit 31)", reasonFlagsText((1u << 18) | (1u << 31)).c_str());
    TEST_ASSERT_EQUAL_STRING("no reason given", reasonFlagsText(0).c_str());
    const char *names[] = {
        "manual override",
        "no data",
        "stale data",
        "sensor fault",
        "cloud cover",
        "sky bright",
        "humidity",
        "dew point",
        "environment sensor fault",
        "rain",
        "rain sensor fault",
        "wind",
        "wind gust",
        "wind sensor fault",
        "battery low",
        "battery data",
        "shutdown",
        "external hold"};
    for (uint32_t bit = 0; bit < 18; ++bit)
    {
        const std::string text = reasonFlagsText(1u << bit);
        TEST_ASSERT_EQUAL_STRING(names[bit], text.c_str());
        TEST_ASSERT_NULL(strstr(text.c_str(), "0x"));
    }
}

void test_describe_says_sqmeters_words(void)
{
    WeatherInputs in = httpSafe();
    SqmeterReply cloud = reply(false, 1u << 4);
    cloud.reasons = "Cloud 100% >= 30%";
    in.http.recordGood(cloud, NOW_MS - 1000);
    WeatherVerdict v = evaluateWeather(in);
    TEST_ASSERT_EQUAL_STRING("Cloud 100% >= 30%", v.http.sqmeterReasons.c_str());
    TEST_ASSERT_EQUAL_STRING("SQMeter HTTP says unsafe: Cloud 100% >= 30%", describeSource(v.http).c_str());
    TEST_ASSERT_EQUAL_STRING("unsafe: SQMeter HTTP says unsafe: Cloud 100% >= 30%", describe(v).c_str());
    in.http.recordGood(reply(false, 0), NOW_MS);
    v = evaluateWeather(in);
    TEST_ASSERT_EQUAL_STRING("SQMeter HTTP says unsafe: no reason given", describeSource(v.http).c_str());
}

// Spec 030 (FR-012): an imaging app's OpenShutter while the verdict is unsafe
// is an Alpaca error naming the reason in words, from SQMeter's reply through
// the verdict's text to the answer; the weather hold never delays it.
void test_open_shutter_while_unsafe_names_the_reason(void)
{
    struct Case
    {
        uint32_t flags;
        const char *reasons;
        const char *expected;
    };
    const Case cases[] = {
        {1u << 4, "Cloud 100% >= 30%", "Weather not safe: unsafe: SQMeter HTTP says unsafe: Cloud 100% >= 30%"},
        {1u << 4, "", "Weather not safe: unsafe: SQMeter HTTP says unsafe: cloud cover"},
        {Safety::SQMETER_RAIN, "", "Weather not safe: unsafe: SQMeter HTTP says unsafe: rain"},
    };
    for (const Case &c : cases)
    {
        Bench b;
        Safety::WeatherInputs in;
        in.http.enabled = true;
        in.nowMs = 100000;
        SqmeterReply sent;
        sent.safe = false;
        sent.reasonFlags = c.flags;
        sent.reasons = c.reasons;
        in.http.recordGood(sent, in.nowMs); // unsafe for no time at all
        const Safety::WeatherVerdict verdict = Safety::evaluateWeather(in);
        RoofIo::GateInputs gates = safeAndParked();
        gates.weather = verdict.verdict;
        gates.weatherText = Safety::describe(verdict);
        gates.weatherReasons = verdict.reasons;
        const RoofIo::CommandCheck check = RoofIo::checkCommand(Command::Open, remote, gates, b.roof, ctx());
        TEST_ASSERT_FALSE(check.accepted);
        const Alpaca::DomeAnswer answer = RoofIo::domeAnswer(Command::Open, check.accepted, check.reason, check.text, {});
        TEST_ASSERT_FALSE(answer.accepted);
        TEST_ASSERT_EQUAL_STRING(c.expected, answer.message.c_str());
        TEST_ASSERT_NULL(strstr(answer.message.c_str(), "0x"));
        TEST_ASSERT_FALSE(run(b, Command::Open, remote, gates));
        expectStill(b);
    }
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_reasons_are_read_joined_and_kept);
    RUN_TEST(test_reasons_drop_control_characters_and_are_capped);
    RUN_TEST(test_reasons_never_cut_a_utf8_character);
    RUN_TEST(test_flags_read_as_words);
    RUN_TEST(test_describe_says_sqmeters_words);
    RUN_TEST(test_open_shutter_while_unsafe_names_the_reason);
    return UNITY_END();
}
