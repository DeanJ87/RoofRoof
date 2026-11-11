#include <unity.h>

#include "AlertEngine.h"

#include <ArduinoJson.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace SQM::Alerts;

// SQMeter spec 008 FR-006 / SC-003: every per-event {variable} the web UI offers
// (lib/AlertLogic/template-variables.json) is one the device fills. The
// common variables are checked in test_device_core (Core::alertVars), the UI
// side in web/src/components/__tests__/alertVariables.test.ts.

void setUp(void) {}
void tearDown(void) {}

namespace
{
    AlertRules quickRules()
    {
        AlertRules rules;
        rules.startupGraceSeconds = 0;
        rules.cooldownSeconds = 0;
        rules.onClientLost = true;
        rules.onClientBack = true;
        rules.onClientDisconnected = true;
        return rules;
    }

    AlertInputs calm(uint32_t now)
    {
        AlertInputs in;
        in.nowSeconds = now;
        in.clientCount = 1;
        in.clients[0].key = "dome";
        in.clients[0].device = "roof";
        return in;
    }

    std::vector<std::string> sharedEventVars(const char *event)
    {
        std::ifstream in("lib/AlertLogic/template-variables.json");
        std::stringstream text;
        text << in.rdbuf();
        DynamicJsonDocument doc(4096);
        TEST_ASSERT_FALSE(deserializeJson(doc, text.str()));
        std::vector<std::string> names;
        for (JsonVariantConst name : doc["events"][event].as<JsonArrayConst>())
            names.push_back(name.as<std::string>());
        return names;
    }

    void assertFillsSharedVars(const std::vector<Alert> &alerts, AlertType type)
    {
        const char *event = alertTypeName(type);
        const Alert *alert = nullptr;
        for (const Alert &a : alerts)
            if (a.type == type)
                alert = &a;
        TEST_ASSERT_NOT_NULL_MESSAGE(alert, event);
        for (const std::string &name : sharedEventVars(event))
        {
            bool filled = false;
            for (const auto &v : alert->vars)
                filled = filled || v.first == name;
            TEST_ASSERT_TRUE_MESSAGE(filled, (std::string(event) + " does not fill {" + name + "}").c_str());
        }
    }
} // namespace

// Every event with its own variables is one the engine can raise.
void test_every_listed_event_exists(void)
{
    std::ifstream in("lib/AlertLogic/template-variables.json");
    std::stringstream text;
    text << in.rdbuf();
    DynamicJsonDocument doc(4096);
    TEST_ASSERT_FALSE(deserializeJson(doc, text.str()));
    for (JsonPairConst event : doc["events"].as<JsonObjectConst>())
    {
        bool known = false;
        for (size_t i = 0; i < ALERT_TYPE_COUNT; ++i)
            known = known || std::string(alertTypeName(static_cast<AlertType>(i))) == event.key().c_str();
        TEST_ASSERT_TRUE_MESSAGE(known, event.key().c_str());
    }
}

void test_client_events_fill_their_variables(void)
{
    const AlertRules rules = quickRules();
    AlertEngine engine;
    auto client = [](uint32_t now, bool watching, bool silent, bool disconnectedNow)
    {
        AlertInputs in = calm(now);
        in.clients[0].watching = watching;
        in.clients[0].silent = silent;
        in.clients[0].disconnectedNow = disconnectedNow;
        in.clients[0].silentFor = "2 min";
        in.clients[0].lastChecked = "21:04";
        return in;
    };
    engine.update(client(0, true, false, false), rules);
    assertFillsSharedVars(engine.update(client(130, true, true, false), rules), AlertType::ClientLost);
    assertFillsSharedVars(engine.update(client(400, true, false, false), rules), AlertType::ClientBack);
    assertFillsSharedVars(engine.update(client(410, false, false, true), rules), AlertType::ClientDisconnected);
}

// The roof's non-critical events (spec 007); the critical ones are checked in
// test_roof_alerts (CloseSeq::criticalAlert).
void test_roof_events_fill_their_variables(void)
{
    const AlertRules rules = quickRules();
    AlertEngine engine;
    AlertInputs in;
    in.nowSeconds = 0;
    in.roof.known = true;
    engine.update(in, rules);
    in.nowSeconds = 100;
    in.roof.faultLatched = true;
    in.roof.supplyLow = true;
    in.roof.sourceLost = true;
    in.roof.closing = true;
    in.roof.closedCount = 1;
    std::vector<Alert> alerts = engine.update(in, rules);
    in.nowSeconds = 100 + ROOF_SETTLE_SECONDS; // the supply and source settle
    const std::vector<Alert> settled = engine.update(in, rules);
    alerts.insert(alerts.end(), settled.begin(), settled.end());
    for (AlertType type :
         {AlertType::MoveFault, AlertType::SupplyLow, AlertType::SafetyLost, AlertType::UnsafeClosing, AlertType::RoofClosed})
        assertFillsSharedVars(alerts, type);
}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_listed_event_exists);
    RUN_TEST(test_client_events_fill_their_variables);
    RUN_TEST(test_roof_events_fill_their_variables);
    return UNITY_END();
}
