// Owner decision 2026-11-24 (specs 022 and 007): on a test box the roof's
// critical alerts ("mount didn't park", "roof didn't close") never wake anyone.
// With the weather bypass in effect, a missing weather source never starts the
// close on unsafe weather, so neither alert is raised; with any bypass in effect
// (or set and waiting for the clock) a critical alert goes out at most at
// urgent, never Pushover's emergency priority. A real install is unchanged.

#include <string>
#include <unity.h>

#include "AlertEngine.h"
#include "CloseSequence.h"
#include "Config.h"
#include "DeviceCore.h"
#include "RoofAlerts.h"
#include "SafetyInput.h"
#include "TestBox.h"

using namespace SQM;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    constexpr int64_t NOW = 1764000000; // 2025-11-24 16:00 UTC
    constexpr uint32_t HOUR = 3600;
    const Core::LocalClock CLOCK{"23:00", "2026-11-24"};

    TestBoxConfig weatherOnly()
    {
        TestBoxConfig t;
        t.weather = true;
        t.until = static_cast<uint32_t>(NOW + 72 * HOUR);
        return t;
    }

    // The weather verdict and park result as SafetyInputs makes them: no SQMeter
    // source, no N.I.N.A., no park switches, and the test box applied.
    struct Readings
    {
        Safety::WeatherVerdict weather;
        Safety::ParkResult park;
    };

    Readings readings(const TestBox::State &box, uint64_t nowMs)
    {
        Safety::WeatherInputs w;
        w.startedAtMs = 0;
        w.nowMs = nowMs;
        Readings r;
        r.weather = Safety::evaluateWeather(w);
        Safety::ParkInputs p;
        p.nowMs = nowMs;
        r.park = Safety::evaluatePark(p);
        Safety::CloseHold hold;
        TestBox::applyWeather(r.weather, box);
        TestBox::applyPark(r.park, hold, box);
        return r;
    }

    // An open roof for an hour, stepped every second: the critical alerts raised.
    int criticalAlerts(const TestBox::State &box)
    {
        CloseSeq::Sequence seq;
        int alerts = 0;
        for (uint64_t ms = 1000; ms <= 3600000; ms += 1000)
        {
            const Readings r = readings(box, ms);
            CloseSeq::Inputs in;
            in.nowMs = ms;
            in.roof = Roof::State::Open;
            CloseSeq::applySafety(in, r.weather.verdict, r.park);
            const CloseSeq::Decision d = seq.step(in);
            if (CloseSeq::isCritical(d.event) && d.alert)
                ++alerts;
        }
        return alerts;
    }

    // The critical alert the close sequence raises for a park not confirmed,
    // finished as the firmware sends it (WebServer::raiseRoofAlert).
    Alerts::Alert sentAlert(const Config &cfg, const TestBox::State &box)
    {
        CloseSeq::Decision d;
        d.event = CloseSeq::Event::ParkNotConfirmed;
        d.cause = CloseSeq::Cause::NinaNotConfigured;
        d.alert = true;
        return Core::finishCriticalAlert(
            CloseSeq::criticalAlert(d, "N.I.N.A. not configured"), Core::AlertSources{cfg}, CLOCK, TestBox::quietsCriticals(box));
    }
} // namespace

void test_weather_bypass_in_effect_starts_no_close_and_raises_no_critical_alert(void)
{
    const TestBox::State box = TestBox::evaluate(weatherOnly(), TestBox::Sources{}, true, NOW);
    TEST_ASSERT_TRUE(box.weather);
    TEST_ASSERT_FALSE(box.park); // no park bypass either: "mount didn't park" would be at once
    const Readings r = readings(box, 1000);
    TEST_ASSERT_TRUE(r.weather.safe());
    TEST_ASSERT_TRUE(r.weather.bypassed);
    TEST_ASSERT_EQUAL(0, criticalAlerts(box));
}

void test_a_real_install_without_a_weather_source_still_raises_it(void)
{
    // No bypass set: "no weather source" is doubt, so unsafe, and with no
    // N.I.N.A. the park can't be confirmed: the critical alert goes out.
    const TestBox::State box = TestBox::evaluate(TestBoxConfig{}, TestBox::Sources{}, true, NOW);
    TEST_ASSERT_FALSE(box.set);
    TEST_ASSERT_TRUE(criticalAlerts(box) >= 1);
    // Expired bypasses are the checks back: the same as a real install.
    const TestBox::State expired = TestBox::evaluate(weatherOnly(), TestBox::Sources{}, true, NOW + 73 * HOUR);
    TEST_ASSERT_TRUE(expired.expired);
    TEST_ASSERT_TRUE(criticalAlerts(expired) >= 1);
}

void test_a_test_box_critical_alert_is_never_an_emergency(void)
{
    Config cfg = Config::createDefault(); // both critical events default to wake me
    TEST_ASSERT_EQUAL(4, cfg.alerts.mountNotParked.level);
    TEST_ASSERT_EQUAL(4, cfg.alerts.roofNotClosed.level);
    TestBoxConfig parkOnly;
    parkOnly.park = true;
    parkOnly.until = static_cast<uint32_t>(NOW + HOUR);
    const TestBox::State cases[] = {
        TestBox::evaluate(weatherOnly(), TestBox::Sources{}, true, NOW), // weather bypass in effect
        TestBox::evaluate(parkOnly, TestBox::Sources{}, true, NOW),      // park bypass in effect
        TestBox::evaluate(weatherOnly(), TestBox::Sources{}, false, 0),  // set, the clock not set after a restart
    };
    for (const TestBox::State &box : cases)
    {
        TEST_ASSERT_TRUE(TestBox::quietsCriticals(box));
        const Alerts::Alert out = sentAlert(cfg, box);
        TEST_ASSERT_EQUAL(static_cast<int>(Alerts::AlertLevel::Urgent), static_cast<int>(out.level));
        TEST_ASSERT_EQUAL(1, Alerts::pushoverPriority(out.level)); // breaks quiet hours, no repeats
    }
    // Set to urgent already: urgent; never lower than the setting's floor.
    cfg.alerts.mountNotParked.level = 3;
    TEST_ASSERT_EQUAL(static_cast<int>(Alerts::AlertLevel::Urgent), static_cast<int>(sentAlert(cfg, cases[0]).level));
}

void test_a_real_install_still_gets_wake_me(void)
{
    Config cfg = Config::createDefault();
    TestBoxConfig parkOnly;
    parkOnly.park = true;
    parkOnly.until = static_cast<uint32_t>(NOW + HOUR);
    const TestBox::State cases[] = {
        TestBox::evaluate(TestBoxConfig{}, TestBox::Sources{}, true, NOW),                  // no bypass set
        TestBox::evaluate(weatherOnly(), TestBox::Sources{}, true, NOW + 73 * HOUR),        // expired: the checks are back
        TestBox::evaluate(parkOnly, TestBox::Sources{true, false, true, false}, true, NOW), // set beside N.I.N.A.: not in effect
    };
    for (const TestBox::State &box : cases)
    {
        TEST_ASSERT_FALSE(TestBox::quietsCriticals(box));
        const Alerts::Alert out = sentAlert(cfg, box);
        TEST_ASSERT_EQUAL(static_cast<int>(Alerts::AlertLevel::Wake), static_cast<int>(out.level));
        TEST_ASSERT_EQUAL(2, Alerts::pushoverPriority(out.level)); // the emergency, repeated until acknowledged
    }
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_weather_bypass_in_effect_starts_no_close_and_raises_no_critical_alert);
    RUN_TEST(test_a_real_install_without_a_weather_source_still_raises_it);
    RUN_TEST(test_a_test_box_critical_alert_is_never_an_emergency);
    RUN_TEST(test_a_real_install_still_gets_wake_me);
    return UNITY_END();
}
