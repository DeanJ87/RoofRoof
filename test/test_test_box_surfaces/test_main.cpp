// Spec 022: how the test box's bypasses show: the status text, the status
// object (also in the MQTT state document), the Dome's description and the
// "Test box mode on" alert (each bypass coming into effect, and after a boot).

#include <cstring>
#include <string>
#include <unity.h>

#include "AlertEngine.h"
#include "AlpacaRouter.h"
#include "Config.h"
#include "DeviceCore.h"
#include "RoofStatus.h"
#include "TestBox.h"

using namespace SQM;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    constexpr int64_t NOW = 1764000000; // 2025-11-24 16:00 UTC
    constexpr uint32_t HOUR = 3600;

    TestBoxConfig allOn(uint32_t until = NOW + 72 * HOUR)
    {
        TestBoxConfig t;
        t.park = t.weather = t.daylight = true;
        t.until = until;
        return t;
    }

    TestBox::State inEffect(const TestBoxConfig &t = allOn(), TestBox::Sources sources = TestBox::Sources{})
    {
        return TestBox::evaluate(t, sources, true, NOW);
    }
} // namespace

// --- Texts, the status object, the Dome ---------------------------------------------

void test_texts(void)
{
    TEST_ASSERT_EQUAL_STRING("2025-11-24 16:00 UTC", TestBox::formatUtc(NOW).c_str());
    const TestBoxConfig t = allOn(NOW + 72 * HOUR);
    TEST_ASSERT_EQUAL_STRING(
        "Test box: park check (no telescope), weather check (no weather source), daylight close bypassed until 2025-11-27 16:00 UTC",
        TestBox::describe(TestBox::evaluate(t, {}, true, NOW)).c_str());
    TEST_ASSERT_EQUAL_STRING(
        "Test box bypasses expired at 2025-11-27 16:00 UTC: the checks are back",
        TestBox::describe(TestBox::evaluate(t, {}, true, NOW + 72 * HOUR)).c_str());
    TEST_ASSERT_EQUAL_STRING(
        "Test box bypasses wait for the clock: the checks apply until it is set",
        TestBox::describe(TestBox::evaluate(t, {}, false, 0)).c_str());
    TestBoxConfig parkOnly;
    parkOnly.park = true;
    parkOnly.until = t.until;
    TEST_ASSERT_EQUAL_STRING(
        "Test box bypasses set but not in effect: a N.I.N.A. address or weather source is set",
        TestBox::describe(TestBox::evaluate(parkOnly, TestBox::Sources{true, false}, true, NOW)).c_str());
    TEST_ASSERT_EQUAL_STRING("", TestBox::describe(TestBox::State{}).c_str());
    // A restore brings no deadline: never "expired at 1970".
    TEST_ASSERT_EQUAL_STRING(
        "Test box bypasses set but not started (restored settings): save or Renew to start them",
        TestBox::describe(TestBox::evaluate(allOn(0), {}, true, NOW)).c_str());
}

void test_the_status_object(void)
{
    DynamicJsonDocument doc(1024);
    RoofIo::writeTestBox(doc.to<JsonObject>(), inEffect(allOn(NOW + 10)));
    JsonObject box = doc["testBox"];
    TEST_ASSERT_TRUE(box["set"].as<bool>() && box["active"].as<bool>());
    TEST_ASSERT_TRUE(box["park"].as<bool>() && box["weather"].as<bool>() && box["daylight"].as<bool>());
    TEST_ASSERT_EQUAL_UINT32(NOW + 10, box["until"].as<uint32_t>());
    TEST_ASSERT_FALSE(box["expired"].as<bool>());
    TEST_ASSERT_FALSE(box["waitingForClock"].as<bool>());
    TEST_ASSERT_NOT_NULL(strstr(box["text"].as<const char *>(), "Test box: park check"));

    DynamicJsonDocument off(512);
    RoofIo::writeTestBox(off.to<JsonObject>(), TestBox::State{});
    TEST_ASSERT_FALSE(off["testBox"]["active"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("", off["testBox"]["text"].as<const char *>());
}

void test_the_status_section_carries_it(void)
{
    RoofIo::Snapshot snap;
    snap.testBox = inEffect();
    DynamicJsonDocument doc(4096);
    RoofIo::writeStatus(doc.to<JsonObject>(), snap);
    TEST_ASSERT_TRUE(doc["testBox"]["active"].as<bool>());
}

void test_the_dome_description(void)
{
    TEST_ASSERT_EQUAL_STRING(
        "TEST BOX (bypassed: park check, weather check, daylight close): Roll-off roof: shutter open, close and stop only",
        Alpaca::domeDeviceInfo("off", TestBox::domePrefix(inEffect())).description.c_str());
    TestBoxConfig parkOnly;
    parkOnly.park = true;
    parkOnly.until = NOW + 1;
    TEST_ASSERT_EQUAL_STRING("TEST BOX (bypassed: park check): ", TestBox::domePrefix(inEffect(parkOnly)).c_str());
    TEST_ASSERT_EQUAL_STRING("", TestBox::domePrefix(TestBox::State{}).c_str());
    TEST_ASSERT_EQUAL_STRING("Roll-off roof: shutter open, close and stop only", Alpaca::domeDeviceInfo().description.c_str());
}

// --- The alert --------------------------------------------------------------------------

namespace
{
    std::vector<Alerts::Alert> pass(Alerts::AlertEngine &engine, uint32_t now, uint8_t bits, bool on = true, bool settled = true)
    {
        Alerts::AlertInputs in;
        in.nowSeconds = now;
        in.roof.known = true;
        in.roof.testBox = bits;
        in.roof.testBoxSettled = settled;
        in.roof.testBoxText = bits ? "Test box: park check (no telescope) bypassed until 2025-11-27 16:00 UTC" : "";
        Alerts::AlertRules rules;
        rules.onTestBox = on;
        return engine.update(in, rules);
    }

    size_t testBoxAlerts(const std::vector<Alerts::Alert> &alerts)
    {
        size_t n = 0;
        for (const Alerts::Alert &a : alerts)
            if (a.type == Alerts::AlertType::TestBox)
                ++n;
        return n;
    }
} // namespace

// A restart with a bypass already in effect sends nothing: it was announced
// when it came into effect (a reversal of spec 022's first rule).
void test_no_alert_after_a_boot_with_a_bypass_in_effect(void)
{
    Alerts::AlertEngine engine;
    TEST_ASSERT_EQUAL_size_t(0, testBoxAlerts(pass(engine, 100, TestBox::BitPark)));
    TEST_ASSERT_EQUAL_size_t(0, testBoxAlerts(pass(engine, 161, TestBox::BitPark)));
    TEST_ASSERT_EQUAL_size_t(0, testBoxAlerts(pass(engine, 400, TestBox::BitPark)));
    // A bypass added later still announces itself.
    TEST_ASSERT_EQUAL_size_t(1, testBoxAlerts(pass(engine, 401, TestBox::BitPark | TestBox::BitWeather)));
}

// Waiting for the clock, nothing is decided: the bypasses that appear once it
// is set are the baseline, not a new bypass.
void test_no_alert_when_the_clock_sets_after_a_boot(void)
{
    Alerts::AlertEngine engine;
    TEST_ASSERT_EQUAL_size_t(0, testBoxAlerts(pass(engine, 100, 0, true, false)));
    TEST_ASSERT_EQUAL_size_t(0, testBoxAlerts(pass(engine, 200, 0, true, false)));
    TEST_ASSERT_EQUAL_size_t(0, testBoxAlerts(pass(engine, 201, TestBox::BitPark)));
    TEST_ASSERT_EQUAL_size_t(0, testBoxAlerts(pass(engine, 300, TestBox::BitPark)));
}

// A first decided pass that disagrees with the next (a copy torn while it was
// written) doesn't set the baseline: no spurious alert after the grace.
void test_a_torn_first_pass_is_not_the_baseline(void)
{
    Alerts::AlertEngine engine;
    pass(engine, 100, 0);
    pass(engine, 101, TestBox::BitPark);
    pass(engine, 102, TestBox::BitPark);
    TEST_ASSERT_EQUAL_size_t(0, testBoxAlerts(pass(engine, 161, TestBox::BitPark)));
    TEST_ASSERT_EQUAL_size_t(0, testBoxAlerts(pass(engine, 300, TestBox::BitPark)));
}

// A bypass switched on during the grace is sent once the grace is over.
void test_the_alert_held_through_the_grace(void)
{
    Alerts::AlertEngine engine;
    pass(engine, 100, 0);
    pass(engine, 101, 0); // the baseline: two decided passes that agree
    TEST_ASSERT_EQUAL_size_t(0, testBoxAlerts(pass(engine, 130, TestBox::BitPark)));
    const std::vector<Alerts::Alert> sent = pass(engine, 161, TestBox::BitPark);
    TEST_ASSERT_EQUAL_size_t(1, testBoxAlerts(sent));
    TEST_ASSERT_EQUAL_STRING("Test box mode on", sent[0].title.c_str());
    TEST_ASSERT_EQUAL_STRING(
        "Checks bypassed for a test box: Test box: park check (no telescope) bypassed until 2025-11-27 16:00 UTC. Faults still stop the "
        "roof.",
        sent[0].message.c_str());
    TEST_ASSERT_EQUAL_size_t(0, testBoxAlerts(pass(engine, 200, TestBox::BitPark))); // once
}

void test_the_alert_on_each_bypass_coming_into_effect(void)
{
    Alerts::AlertEngine engine;
    pass(engine, 100, 0);
    TEST_ASSERT_EQUAL_size_t(0, testBoxAlerts(pass(engine, 200, 0)));
    TEST_ASSERT_EQUAL_size_t(1, testBoxAlerts(pass(engine, 201, TestBox::BitPark)));
    TEST_ASSERT_EQUAL_size_t(1, testBoxAlerts(pass(engine, 202, TestBox::BitPark | TestBox::BitWeather))); // a new one
    TEST_ASSERT_EQUAL_size_t(0, testBoxAlerts(pass(engine, 203, TestBox::BitWeather)));                    // one off
    TEST_ASSERT_EQUAL_size_t(0, testBoxAlerts(pass(engine, 204, 0)));
    TEST_ASSERT_EQUAL_size_t(1, testBoxAlerts(pass(engine, 205, TestBox::BitWeather))); // on again
}

void test_the_alert_rule_off_sends_nothing(void)
{
    Alerts::AlertEngine engine;
    pass(engine, 100, 0, false);
    TEST_ASSERT_EQUAL_size_t(0, testBoxAlerts(pass(engine, 200, TestBox::BitPark, false)));
    TEST_ASSERT_EQUAL_STRING("test_box", Alerts::alertTypeName(Alerts::AlertType::TestBox));
}

// Held by the master switch, the pause and the send mode like any other
// alert, and stacked with them.
void test_the_alert_obeys_alerts_off_and_paused(void)
{
    AlertsConfig off = Config::createDefault().alerts;
    off.enabled = false;
    Alerts::Alert box;
    box.type = Alerts::AlertType::TestBox;
    Alerts::Alert fault;
    fault.type = Alerts::AlertType::MoveFault;
    TEST_ASSERT_EQUAL_size_t(0, Core::alertsToSend({fault, box}, off, true).size());
    TEST_ASSERT_FALSE(Core::alertGoesOut(Alerts::AlertType::TestBox, off, true));
    AlertsConfig on = off;
    on.enabled = true;
    TEST_ASSERT_EQUAL_size_t(0, Core::alertsToSend({fault, box}, on, false).size()); // paused
    TEST_ASSERT_FALSE(Core::alertGoesOut(Alerts::AlertType::TestBox, on, false));
    const std::vector<Alerts::Alert> sent = Core::alertsToSend({fault, box}, on, true);
    TEST_ASSERT_EQUAL_size_t(1, sent.size()); // one stacked notification
    TEST_ASSERT_TRUE(Core::alertGoesOut(Alerts::AlertType::TestBox, on, true));
    // The critical roof alerts are still past both.
    TEST_ASSERT_TRUE(Core::alertGoesOut(Alerts::AlertType::RoofNotClosed, off, false));
}

// Never an emergency (Pushover priority 2 repeats every minute for an hour):
// a stored "wake me" is sent as urgent, and raised from storage as urgent.
void test_the_alert_is_never_wake_me(void)
{
    Config cfg = Config::createDefault();
    cfg.alerts.testBox.level = 4;
    Alerts::AlertEngine engine;
    Alerts::AlertInputs in;
    in.roof.known = true;
    in.roof.testBoxSettled = true;
    Alerts::AlertRules rules;
    std::vector<Alerts::Alert> sent;
    for (uint32_t now : {100u, 101u, 200u})
    {
        in.nowSeconds = now;
        in.roof.testBox = now == 200 ? TestBox::BitPark : 0;
        in.roof.testBoxText = "Test box";
        sent = Core::runAlerts(engine, in, rules, Core::AlertSources{cfg}, Core::LocalClock{}).outgoing;
    }
    TEST_ASSERT_EQUAL_size_t(1, testBoxAlerts(sent));
    TEST_ASSERT_TRUE(sent[0].level == Alerts::AlertLevel::Urgent);
    TEST_ASSERT_EQUAL_INT(1, Alerts::pushoverPriority(sent[0].level));
    cfg.alerts.raiseCriticalToMinimum();
    TEST_ASSERT_EQUAL_UINT8(AlertsConfig::MAX_TEST_BOX_LEVEL, cfg.alerts.testBox.level);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_alert_obeys_alerts_off_and_paused);
    RUN_TEST(test_texts);
    RUN_TEST(test_the_status_object);
    RUN_TEST(test_the_status_section_carries_it);
    RUN_TEST(test_the_dome_description);
    RUN_TEST(test_no_alert_after_a_boot_with_a_bypass_in_effect);
    RUN_TEST(test_no_alert_when_the_clock_sets_after_a_boot);
    RUN_TEST(test_a_torn_first_pass_is_not_the_baseline);
    RUN_TEST(test_the_alert_held_through_the_grace);
    RUN_TEST(test_the_alert_on_each_bypass_coming_into_effect);
    RUN_TEST(test_the_alert_rule_off_sends_nothing);
    RUN_TEST(test_the_alert_is_never_wake_me);
    return UNITY_END();
}
