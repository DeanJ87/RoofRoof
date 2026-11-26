// Spec 039 (finding F31): an Alpaca or MQTT Open refused for the weather says
// the weather source's own reasons, not only "unsafe". SQMeter's /api/safety
// document as SQMeter writes it (a hold set from Home Assistant, its holds and
// rules not in effect), read over HTTP and over MQTT (<base>/safety then
// <base>/safe), through the verdict's text to the Alpaca error, the MQTT
// result and an imaging app's refused stop of a close.

#include <cstring>
#include <string>
#include <unity.h>

#include "CommandCheck.h"
#include "Config.h" // RoofIoLogic builds lib/ConfigModel (SettingsGuard); name it so the native build finds its dependencies
#include "DomeBridge.h"
#include "RoofController.h"
#include "RoofMqtt.h"
#include "SafetyInput.h"
#include "SqmeterReply.h"
#include "SqmeterSource.h"

#include "../roof_sim/RoofSim.h"

using namespace SQM;
using Roof::Command;
using Roof::Origin;
using RoofTest::Bench;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    constexpr const char *HOLD = "Hold: Owner away (set from Home Assistant)";
    // SQMeter's writeSafety (SQMeter lib/DeviceCore/src/DeviceSafety.cpp): bit 17 is its external hold.
    constexpr const char *DOCUMENT =
        R"j({"safe":false,"rawSafe":false,"alpacaEnabled":true,"reasonFlags":131072,)j"
        R"j("reasons":["Hold: Owner away (set from Home Assistant)"],"secondsUntilSafe":0,)j"
        R"j("secondsUntilUnsafe":0,"rulesNotInEffect":["Rain: the rain sensor is off"],)j"
        R"j("evaluatedAgeMs":120,"changedAgeMs":5000,"dewSuspected":false,)j"
        R"j("holds":[{"id":"away","reason":"Owner away","source":"Home Assistant","sinceEpoch":1764000000}]})j";
    constexpr uint64_t NOW_MS = 100000;

    Safety::WeatherVerdict overHttp()
    {
        Safety::WeatherInputs in;
        in.http.enabled = true;
        in.nowMs = NOW_MS;
        Safety::HttpOutcome reply;
        reply.status = 200;
        reply.body = DOCUMENT;
        Safety::recordHttpReply(in.http, reply, in.nowMs);
        return Safety::evaluateWeather(in);
    }

    Safety::WeatherVerdict overMqtt()
    {
        Safety::WeatherInputs in;
        in.mqtt.enabled = true;
        in.mqtt.availability = Safety::Availability::Online;
        in.nowMs = NOW_MS;
        const Safety::MqttTopics topics = Safety::MqttTopics::under("sqmeter");
        Safety::applyMqttMessage(in.mqtt, topics, topics.safety, DOCUMENT, in.nowMs);
        Safety::applyMqttMessage(in.mqtt, topics, topics.safe, "0", in.nowMs);
        return Safety::evaluateWeather(in);
    }

    RoofIo::GateInputs gatesFor(const Safety::WeatherVerdict &verdict)
    {
        RoofIo::GateInputs g;
        g.weather = verdict.verdict;
        g.weatherText = Safety::describe(verdict);
        g.weatherReasons = verdict.reasons;
        g.parked = true;
        g.parkText = "parked";
        return g;
    }

    bool hasHold(const std::string &text)
    {
        return std::strstr(text.c_str(), HOLD) != nullptr;
    }
} // namespace

void test_the_verdict_says_sqmeters_reasons_over_http_and_mqtt(void)
{
    const Safety::WeatherVerdict http = overHttp();
    TEST_ASSERT_TRUE(http.verdict == Safety::Verdict::Unsafe);
    TEST_ASSERT_EQUAL_STRING(
        "unsafe: SQMeter HTTP says unsafe: Hold: Owner away (set from Home Assistant)", Safety::describe(http).c_str());
    const Safety::WeatherVerdict mqtt = overMqtt();
    TEST_ASSERT_TRUE(mqtt.verdict == Safety::Verdict::Unsafe);
    TEST_ASSERT_EQUAL_STRING(
        "unsafe: SQMeter MQTT says unsafe: Hold: Owner away (set from Home Assistant)", Safety::describe(mqtt).c_str());
}

// The Open refused from an imaging app (Alpaca) and from MQTT / Home Assistant.
void test_an_open_refused_for_the_weather_names_the_hold(void)
{
    for (const Safety::WeatherVerdict &verdict : {overHttp(), overMqtt()})
    {
        const RoofIo::GateInputs gates = gatesFor(verdict);
        for (Origin origin : {Origin::Client, Origin::Mqtt})
        {
            Bench b; // closed at its magnet
            const RoofIo::CommandCheck check = RoofIo::checkCommand(Command::Open, origin, gates, b.roof, RoofIo::CommandContext{});
            TEST_ASSERT_FALSE(check.accepted);
            TEST_ASSERT_EQUAL_STRING("weatherNotSafe", check.reason.c_str());
            TEST_ASSERT_TRUE_MESSAGE(hasHold(check.text), check.text.c_str());
            TEST_ASSERT_EQUAL_STRING(("Weather not safe: " + Safety::describe(verdict)).c_str(), check.text.c_str());
            // Alpaca: the error message N.I.N.A. shows.
            const Alpaca::DomeAnswer answer = RoofIo::domeAnswer(Command::Open, false, check.reason, check.text, {});
            TEST_ASSERT_TRUE(hasHold(answer.message));
            // MQTT: the result document Home Assistant and automations read.
            RoofIo::Mqtt::Result result;
            result.command = "open";
            result.reason = check.reason;
            result.text = check.text;
            StaticJsonDocument<512> doc;
            RoofIo::Mqtt::writeResult(doc.to<JsonObject>(), result);
            TEST_ASSERT_TRUE(hasHold(doc["text"].as<std::string>()));
        }
    }
}

// The same words when an imaging app's stop of a close is refused for the weather.
void test_a_refused_stop_of_a_close_names_the_hold(void)
{
    Bench b(Roof::Settings(), 1850.0f); // open
    TEST_ASSERT_TRUE(b.command(Command::Close, Origin::Automatic).accepted);
    b.steps(10);
    const RoofIo::GateInputs gates = gatesFor(overHttp());
    const RoofIo::CommandCheck check = RoofIo::checkCommand(Command::Stop, Origin::Client, gates, b.roof, RoofIo::CommandContext{});
    TEST_ASSERT_FALSE(check.accepted);
    TEST_ASSERT_TRUE_MESSAGE(hasHold(check.text), check.text.c_str());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_verdict_says_sqmeters_reasons_over_http_and_mqtt);
    RUN_TEST(test_an_open_refused_for_the_weather_names_the_hold);
    RUN_TEST(test_a_refused_stop_of_a_close_names_the_hold);
    return UNITY_END();
}
