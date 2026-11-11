#include <unity.h>
#include <cstring>
#include "AlertEngine.h"

// The alert engine's generic parts: names, templates, stacking, levels and
// what is waiting to be sent. The imaging-app events are in test_alert_clients;
// the roof's events come with spec 007.

using namespace SQM::Alerts;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    AlertRules rulesWithCooldown(uint32_t cooldown)
    {
        AlertRules rules;
        rules.startupGraceSeconds = 0;
        rules.cooldownSeconds = cooldown;
        rules.onClientLost = true;
        rules.onClientBack = true;
        return rules;
    }

    // One watched device, silent or not.
    AlertInputs inputs(uint32_t now, bool silent)
    {
        AlertInputs in;
        in.nowSeconds = now;
        in.clientCount = 1;
        in.clients[0].key = "dome";
        in.clients[0].device = "roof";
        in.clients[0].watching = true;
        in.clients[0].silent = silent;
        return in;
    }

    bool hasType(const std::vector<Alert> &alerts, AlertType type)
    {
        for (const Alert &a : alerts)
            if (a.type == type)
                return true;
        return false;
    }
} // namespace

void test_alert_type_names(void)
{
    TEST_ASSERT_EQUAL_STRING("alerts_on", alertTypeName(AlertType::AlertsOn));
    TEST_ASSERT_EQUAL_STRING("test", alertTypeName(AlertType::Test));
    TEST_ASSERT_EQUAL_STRING("client_lost", alertTypeName(AlertType::ClientLost));
    TEST_ASSERT_EQUAL_STRING("unknown", alertTypeName(static_cast<AlertType>(ALERT_TYPE_COUNT)));
    TEST_ASSERT_EQUAL_STRING("• a\n• b", joinReasons({"a", "b"}).c_str());
}

void test_render_template(void)
{
    const std::vector<std::pair<std::string, std::string>> vars = {{"silent_for", "2 min"}, {"device", "Roof"}};
    TEST_ASSERT_EQUAL_STRING("Roof: quiet for 2 min", renderTemplate("{device}: quiet for {silent_for}", vars).c_str());
    // Unknown names and stray braces stay visible.
    TEST_ASSERT_EQUAL_STRING("{nope} { x", renderTemplate("{nope} { x", vars).c_str());
    TEST_ASSERT_EQUAL_STRING("", renderTemplate("", vars).c_str());
}

void test_stack_alerts(void)
{
    Alert lost;
    lost.type = AlertType::ClientLost;
    lost.level = AlertLevel::Urgent;
    lost.title = "Imaging app stopped checking";
    lost.message = "lost";
    Alert disconnected;
    disconnected.type = AlertType::ClientDisconnected;
    disconnected.level = AlertLevel::Wake;
    disconnected.sound = "siren";
    disconnected.title = "Imaging app disconnected";
    disconnected.message = "gone";

    const Alert one = stackAlerts({lost});
    TEST_ASSERT_EQUAL_STRING("Imaging app stopped checking", one.title.c_str());
    TEST_ASSERT_EQUAL(0, one.stacked.size());

    const Alert both = stackAlerts({lost, disconnected});
    TEST_ASSERT_EQUAL(static_cast<int>(AlertType::ClientDisconnected), static_cast<int>(both.type));
    TEST_ASSERT_EQUAL(static_cast<int>(AlertLevel::Wake), static_cast<int>(both.level));
    TEST_ASSERT_EQUAL_STRING("siren", both.sound.c_str());
    TEST_ASSERT_EQUAL_STRING("Imaging app disconnected · Imaging app stopped checking", both.title.c_str());
    TEST_ASSERT_EQUAL_STRING("gone\n\nlost", both.message.c_str());
    TEST_ASSERT_EQUAL(1, both.stacked.size());
    TEST_ASSERT_EQUAL(static_cast<int>(AlertType::ClientLost), static_cast<int>(both.stacked[0]));
    TEST_ASSERT_EQUAL_STRING("", stackAlerts({}).title.c_str());
}

void test_level_priorities()
{
    TEST_ASSERT_EQUAL(-1, pushoverPriority(AlertLevel::Quiet));
    TEST_ASSERT_EQUAL(0, pushoverPriority(AlertLevel::Normal));
    TEST_ASSERT_EQUAL(1, pushoverPriority(AlertLevel::Urgent));
    TEST_ASSERT_EQUAL(2, pushoverPriority(AlertLevel::Wake));
    TEST_ASSERT_EQUAL_STRING("low", ntfyPriority(AlertLevel::Quiet));
    TEST_ASSERT_EQUAL_STRING("default", ntfyPriority(AlertLevel::Normal));
    TEST_ASSERT_EQUAL_STRING("high", ntfyPriority(AlertLevel::Urgent));
    TEST_ASSERT_EQUAL_STRING("max", ntfyPriority(AlertLevel::Wake));
    TEST_ASSERT_EQUAL_STRING("quiet", alertLevelName(AlertLevel::Quiet));
    TEST_ASSERT_EQUAL_STRING("off", alertLevelName(AlertLevel::Off));
}

void test_waits_startup_grace(void)
{
    AlertEngine engine;
    AlertRules rules = rulesWithCooldown(60);
    rules.startupGraceSeconds = 60;
    TEST_ASSERT_EQUAL(0, engine.waits(0, rules).size()); // not started
    engine.update(inputs(0, false), rules);
    std::vector<Wait> waits = engine.waits(20, rules);
    TEST_ASSERT_EQUAL(1, waits.size());
    TEST_ASSERT_EQUAL_STRING("startup", waits[0].condition.c_str());
    TEST_ASSERT_TRUE(waits[0].kind == WaitKind::Grace);
    TEST_ASSERT_EQUAL(40, waits[0].remainingSeconds);
    TEST_ASSERT_EQUAL(0, engine.waits(60, rules).size());
}

// A change held back by the cooldown is reported as waiting, and sent once
// the cooldown ends if it still holds.
void test_waits_cooldown_then_sends_the_final_state(void)
{
    AlertEngine engine;
    const AlertRules rules = rulesWithCooldown(60);
    engine.update(inputs(0, false), rules);
    TEST_ASSERT_EQUAL(0, engine.waits(0, rules).size()); // nothing pending

    TEST_ASSERT_TRUE(hasType(engine.update(inputs(10, true), rules), AlertType::ClientLost));
    TEST_ASSERT_EQUAL(0, engine.waits(10, rules).size()); // announced, nothing waiting

    // "Is back" answers at once; a second loss inside the cooldown waits.
    TEST_ASSERT_TRUE(hasType(engine.update(inputs(15, false), rules), AlertType::ClientBack));
    TEST_ASSERT_FALSE(hasType(engine.update(inputs(25, true), rules), AlertType::ClientLost));
    std::vector<Wait> waits = engine.waits(25, rules);
    TEST_ASSERT_EQUAL(1, waits.size());
    TEST_ASSERT_EQUAL_STRING("client:dome", waits[0].condition.c_str());
    TEST_ASSERT_TRUE(waits[0].kind == WaitKind::Cooldown);
    TEST_ASSERT_EQUAL(45, waits[0].remainingSeconds);
    TEST_ASSERT_TRUE(hasType(engine.update(inputs(70, true), rules), AlertType::ClientLost));
}

void test_waits_does_not_change_what_is_sent(void)
{
    const AlertRules rules = rulesWithCooldown(60);
    AlertEngine a;
    AlertEngine b;
    a.update(inputs(0, false), rules);
    b.update(inputs(0, false), rules);
    (void)a.waits(3, rules);
    TEST_ASSERT_EQUAL(a.update(inputs(5, true), rules).size(), b.update(inputs(5, true), rules).size());
    (void)a.waits(8, rules);
    TEST_ASSERT_EQUAL(a.update(inputs(70, false), rules).size(), b.update(inputs(70, false), rules).size());
}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_alert_type_names);
    RUN_TEST(test_render_template);
    RUN_TEST(test_stack_alerts);
    RUN_TEST(test_level_priorities);
    RUN_TEST(test_waits_startup_grace);
    RUN_TEST(test_waits_cooldown_then_sends_the_final_state);
    RUN_TEST(test_waits_does_not_change_what_is_sent);
    return UNITY_END();
}
