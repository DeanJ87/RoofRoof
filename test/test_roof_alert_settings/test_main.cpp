#include <unity.h>

#include <ArduinoJson.h>

#include <cstring>
#include <string>

#include "DeviceCore.h"

// The roof's alert settings (spec 007): defaults, the settings round trip and
// an old config, the critical level floor in validation and at send
// (FR-003), the critical wording rules (FR-005), the engine rules from the
// levels, and the test samples (FR-015).

using namespace SQM;

void setUp() {}
void tearDown() {}

namespace
{
    const char *const ROOF_KEYS[] = {
        "mount_not_parked", "roof_not_closed", "move_fault", "supply_low", "unsafe_closing", "roof_closed", "safety_lost"};

    std::string rejectReason(const std::string &json)
    {
        Config cfg = Config::createDefault();
        std::string error;
        if (Config::applyJson(json, cfg, false, &error))
            return "";
        return error.empty() ? "rejected" : error;
    }

    std::string levelJson(const char *key, int level)
    {
        return std::string("{\"alerts\":{\"events\":{\"") + key + "\":{\"level\":" + std::to_string(level) + "}}}}";
    }

    // A critical alert as the close sequence hands it over.
    Alerts::Alert fromSequence(Alerts::AlertType type, bool reminder = false)
    {
        Alerts::Alert alert;
        alert.type = type;
        alert.level = Alerts::AlertLevel::Wake;
        alert.title = reminder ? "Still: Mount didn't park" : "Mount didn't park";
        alert.message =
            "The weather is unsafe and the park wasn't confirmed within 240 s, so the roof stays open. Park: N.I.N.A. unreachable";
        alert.vars = {
            {"reason", "N.I.N.A. unreachable"}, {"limit", "240 s"}, {"details", alert.message}, {"reminder", reminder ? "yes" : "no"}};
        return alert;
    }

    const Core::LocalClock CLOCK{"23:00", "2026-11-11"};
} // namespace

void test_roof_event_defaults()
{
    const Config cfg = Config::createDefault();
    const AlertsConfig &a = cfg.alerts;
    TEST_ASSERT_EQUAL_UINT8(4, a.mountNotParked.level);
    TEST_ASSERT_EQUAL_UINT8(4, a.roofNotClosed.level);
    TEST_ASSERT_EQUAL_UINT8(3, a.moveFault.level);
    TEST_ASSERT_EQUAL_UINT8(2, a.supplyLow.level);
    TEST_ASSERT_EQUAL_UINT8(2, a.unsafeClosing.level);
    TEST_ASSERT_EQUAL_UINT8(2, a.roofClosed.level);
    TEST_ASSERT_EQUAL_UINT8(2, a.safetyLost.level);
    DynamicJsonDocument doc(16384);
    deserializeJson(doc, cfg.toJson());
    for (const char *key : ROOF_KEYS)
        TEST_ASSERT_FALSE_MESSAGE(doc["alerts"]["events"][key]["level"].isNull(), key);
    // The roof events are in the main alerts part, not the imaging-app part.
    const std::string mainPart = cfg.alertsToJson(false, Config::AlertsPart::Main);
    const std::string clientPart = cfg.alertsToJson(false, Config::AlertsPart::Client);
    for (const char *key : ROOF_KEYS)
    {
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(mainPart.c_str(), key), key);
        TEST_ASSERT_NULL_MESSAGE(strstr(clientPart.c_str(), key), key);
    }
}

void test_roof_event_round_trip_and_old_config()
{
    Config cfg = Config::createDefault();
    std::string error;
    const std::string json = "{\"alerts\":{\"events\":{\"move_fault\":{\"level\":0,\"sound\":\"siren\",\"title\":\"Fault {reason}\","
                             "\"message\":\"m\"},\"roof_not_closed\":{\"level\":3}}}}";
    TEST_ASSERT_TRUE_MESSAGE(Config::applyJson(json, cfg, false, &error), error.c_str());
    TEST_ASSERT_EQUAL_UINT8(0, cfg.alerts.moveFault.level);
    TEST_ASSERT_EQUAL_STRING("siren", cfg.alerts.moveFault.sound.c_str());
    TEST_ASSERT_EQUAL_UINT8(3, cfg.alerts.roofNotClosed.level);
    const std::optional<Config> back = Config::fromJson(cfg.toJson(), nullptr, &error);
    TEST_ASSERT_TRUE_MESSAGE(back.has_value(), error.c_str());
    TEST_ASSERT_EQUAL_STRING("Fault {reason}", back->alerts.moveFault.title.c_str());
    TEST_ASSERT_EQUAL_UINT8(3, back->alerts.roofNotClosed.level);

    // A config saved before spec 007 (no roof events) loads with the defaults.
    const std::optional<Config> old =
        Config::fromJson("{\"alerts\":{\"enabled\":true,\"events\":{\"client_lost\":{\"level\":2}}}}", nullptr, &error);
    TEST_ASSERT_TRUE_MESSAGE(old.has_value(), error.c_str());
    TEST_ASSERT_EQUAL_UINT8(4, old->alerts.mountNotParked.level);
    TEST_ASSERT_EQUAL_UINT8(3, old->alerts.moveFault.level);
    TEST_ASSERT_EQUAL_UINT8(2, old->alerts.clientLost.level);
}

void test_critical_level_floor_in_validation()
{
    for (const char *key : {"mount_not_parked", "roof_not_closed"})
    {
        for (int level = 0; level <= 2; ++level)
            TEST_ASSERT_EQUAL_STRING_MESSAGE(
                "Alerts: the roof's critical alerts are urgent or wake me", rejectReason(levelJson(key, level)).c_str(), key);
        TEST_ASSERT_EQUAL_STRING("", rejectReason(levelJson(key, 3)).c_str());
        TEST_ASSERT_EQUAL_STRING("", rejectReason(levelJson(key, 4)).c_str());
        TEST_ASSERT_TRUE(rejectReason(levelJson(key, 5)).size() > 0);
    }
    for (const char *key : {"move_fault", "supply_low", "unsafe_closing", "roof_closed", "safety_lost"})
    {
        TEST_ASSERT_EQUAL_STRING_MESSAGE("", rejectReason(levelJson(key, 0)).c_str(), key);
        TEST_ASSERT_TRUE(rejectReason(levelJson(key, 5)).size() > 0);
    }
}

void test_full_roof_templates_fit()
{
    Config cfg = Config::createDefault();
    const std::string title(AlertsConfig::MAX_TEMPLATE_TITLE, 'T');
    const std::string message(AlertsConfig::MAX_TEMPLATE_MESSAGE, 'M');
    AlertsConfig &a = cfg.alerts;
    for (AlertsConfig::EventSetting *event :
         {&a.mountNotParked, &a.roofNotClosed, &a.moveFault, &a.supplyLow, &a.unsafeClosing, &a.roofClosed, &a.safetyLost})
        *event = {4, "persistent", title, message};
    std::string error;
    TEST_ASSERT_TRUE_MESSAGE(cfg.validate(&error), error.c_str());
}

// The settings documents have room for every event: nothing is cut off the
// end (the alert channels come last).
void test_settings_documents_are_not_cut_short()
{
    Config cfg = Config::createDefault();
    const std::string title(AlertsConfig::MAX_TEMPLATE_TITLE, 'T');
    const std::string message(AlertsConfig::MAX_TEMPLATE_MESSAGE, 'M');
    AlertsConfig &a = cfg.alerts;
    for (AlertsConfig::EventSetting *event :
         {&a.mountNotParked,
          &a.roofNotClosed,
          &a.moveFault,
          &a.supplyLow,
          &a.unsafeClosing,
          &a.roofClosed,
          &a.safetyLost,
          &a.clientLost,
          &a.clientBack,
          &a.clientDisconnected})
        *event = {4, "persistent", title, message};
    a.mqttEnabled = true;
    a.webhookEnabled = true;
    a.webhookUrl = "http://192.168.1.10/hook";
    a.webhookInsecureTls = true;
    std::string error;
    for (const std::string &json :
         {cfg.toJson(), cfg.alertsToJson(false, Config::AlertsPart::All), cfg.alertsToJson(false, Config::AlertsPart::Main)})
    {
        DynamicJsonDocument doc(16384);
        TEST_ASSERT_FALSE(deserializeJson(doc, json));
        JsonVariantConst alerts = doc.containsKey("alerts") ? doc["alerts"].as<JsonVariantConst>() : doc.as<JsonVariantConst>();
        TEST_ASSERT_TRUE_MESSAGE(alerts["mqtt"]["enabled"].as<bool>(), json.c_str());
        TEST_ASSERT_TRUE(alerts["webhook"]["insecureTls"].as<bool>());
        TEST_ASSERT_EQUAL_STRING(message.c_str(), alerts["events"]["safety_lost"]["message"] | "");
    }
    const std::optional<Config> back = Config::fromJson(cfg.toJson(), nullptr, &error);
    TEST_ASSERT_TRUE_MESSAGE(back.has_value(), error.c_str());
    TEST_ASSERT_TRUE(back->alerts.mqttEnabled);
    TEST_ASSERT_EQUAL_STRING(message.c_str(), back->alerts.clientDisconnected.message.c_str());
}

// --- Critical alerts at send (FR-003 to FR-005) ---

void test_critical_level_floor_at_send()
{
    for (Alerts::AlertType type : {Alerts::AlertType::MountNotParked, Alerts::AlertType::RoofNotClosed})
        for (uint8_t level = 0; level <= 6; ++level)
        {
            Config cfg = Config::createDefault();
            cfg.alerts.mountNotParked.level = level; // as if stored without the rule
            cfg.alerts.roofNotClosed.level = level;
            // The pause, grace and master switch are not inputs here at all.
            cfg.alerts.enabled = (level % 2) == 0;
            const Alerts::Alert out = Core::finishCriticalAlert(fromSequence(type), Core::AlertSources{cfg}, CLOCK);
            const Alerts::AlertLevel want = level >= 4 ? Alerts::AlertLevel::Wake : Alerts::AlertLevel::Urgent;
            TEST_ASSERT_EQUAL(static_cast<int>(want), static_cast<int>(out.level));
            TEST_ASSERT_EQUAL(static_cast<int>(type), static_cast<int>(out.type));
        }
}

void test_critical_built_in_wording_and_sound()
{
    Config cfg = Config::createDefault();
    cfg.alerts.mountNotParked.sound = "siren";
    const Alerts::Alert in = fromSequence(Alerts::AlertType::MountNotParked);
    const Alerts::Alert out = Core::finishCriticalAlert(in, Core::AlertSources{cfg}, CLOCK);
    TEST_ASSERT_EQUAL_STRING(in.title.c_str(), out.title.c_str());
    TEST_ASSERT_EQUAL_STRING(in.message.c_str(), out.message.c_str());
    TEST_ASSERT_EQUAL_STRING("siren", out.sound.c_str());
    TEST_ASSERT_EQUAL(0, out.vars.size());
}

void test_critical_custom_wording_keeps_the_reason()
{
    Config cfg = Config::createDefault();
    const Alerts::Alert in = fromSequence(Alerts::AlertType::MountNotParked);

    cfg.alerts.mountNotParked.title = "{device}: park failed";
    cfg.alerts.mountNotParked.message = "Check the mount. {reason} after {limit}";
    Alerts::Alert out = Core::finishCriticalAlert(in, Core::AlertSources{cfg}, CLOCK);
    TEST_ASSERT_EQUAL_STRING("RoofRoof: park failed", out.title.c_str());
    TEST_ASSERT_EQUAL_STRING("Check the mount. N.I.N.A. unreachable after 240 s", out.message.c_str());

    cfg.alerts.mountNotParked.message = "{details}";
    out = Core::finishCriticalAlert(in, Core::AlertSources{cfg}, CLOCK);
    TEST_ASSERT_EQUAL_STRING(in.message.c_str(), out.message.c_str());

    // Neither {reason} nor {details}: the built-in message is added.
    cfg.alerts.mountNotParked.message = "Go and look ({time})";
    out = Core::finishCriticalAlert(in, Core::AlertSources{cfg}, CLOCK);
    TEST_ASSERT_EQUAL_STRING(("Go and look (23:00)\n\n" + in.message).c_str(), out.message.c_str());
    cfg.alerts.mountNotParked.message = "{reasons} typo";
    out = Core::finishCriticalAlert(in, Core::AlertSources{cfg}, CLOCK);
    TEST_ASSERT_NOT_NULL(strstr(out.message.c_str(), "N.I.N.A. unreachable"));
}

void test_critical_reminder_title_keeps_still()
{
    Config cfg = Config::createDefault();
    cfg.alerts.mountNotParked.title = "Park failed";
    Alerts::Alert out = Core::finishCriticalAlert(fromSequence(Alerts::AlertType::MountNotParked, true), Core::AlertSources{cfg}, CLOCK);
    TEST_ASSERT_EQUAL_STRING("Still: Park failed", out.title.c_str());
    cfg.alerts.mountNotParked.title = "Still: park failed";
    out = Core::finishCriticalAlert(fromSequence(Alerts::AlertType::MountNotParked, true), Core::AlertSources{cfg}, CLOCK);
    TEST_ASSERT_EQUAL_STRING("Still: park failed", out.title.c_str());
    // Built-in title: the sequence's own "Still: ".
    cfg.alerts.mountNotParked.title = "";
    out = Core::finishCriticalAlert(fromSequence(Alerts::AlertType::MountNotParked, true), Core::AlertSources{cfg}, CLOCK);
    TEST_ASSERT_EQUAL_STRING("Still: Mount didn't park", out.title.c_str());
}

// --- Rules and samples ---

void test_rules_follow_the_roof_levels()
{
    Config cfg = Config::createDefault();
    Alerts::AlertRules rules = Core::alertRules(cfg);
    TEST_ASSERT_TRUE(rules.onMoveFault && rules.onSupplyLow && rules.onUnsafeClosing && rules.onRoofClosed && rules.onSafetyLost);
    cfg.alerts.moveFault.level = 0;
    cfg.alerts.supplyLow.level = 0;
    cfg.alerts.unsafeClosing.level = 0;
    cfg.alerts.roofClosed.level = 0;
    cfg.alerts.safetyLost.level = 0;
    rules = Core::alertRules(cfg);
    TEST_ASSERT_FALSE(rules.onMoveFault || rules.onSupplyLow || rules.onUnsafeClosing || rules.onRoofClosed || rules.onSafetyLost);
    for (size_t i = 0; i < Alerts::ALERT_TYPE_COUNT; ++i)
    {
        const Alerts::AlertType type = static_cast<Alerts::AlertType>(i);
        if (type == Alerts::AlertType::AlertsOn || type == Alerts::AlertType::Test)
            continue;
        TEST_ASSERT_NOT_NULL_MESSAGE(Core::eventSettingFor(cfg.alerts, type), Alerts::alertTypeName(type));
    }
}

void test_engine_roof_alerts_get_their_settings()
{
    Config cfg = Config::createDefault();
    cfg.alerts.moveFault = {4, "alien", "{device} fault", "{reason} at {time}"};
    Alerts::AlertEngine engine;
    Alerts::AlertInputs in;
    in.roof.known = true;
    in.roof.faultLatched = true;
    in.roof.fault = "Timeout 61.0 s > 60.0 s";
    in.nowSeconds = 100;
    Alerts::AlertRules rules = Core::alertRules(cfg);
    rules.startupGraceSeconds = 0;
    const Core::AlertStep step = Core::runAlerts(engine, in, rules, Core::AlertSources{cfg}, CLOCK);
    TEST_ASSERT_EQUAL(1, step.outgoing.size());
    TEST_ASSERT_EQUAL(static_cast<int>(Alerts::AlertLevel::Wake), static_cast<int>(step.outgoing[0].level));
    TEST_ASSERT_EQUAL_STRING("alien", step.outgoing[0].sound.c_str());
    TEST_ASSERT_EQUAL_STRING("RoofRoof fault", step.outgoing[0].title.c_str());
    TEST_ASSERT_EQUAL_STRING("Timeout 61.0 s > 60.0 s at 23:00", step.outgoing[0].message.c_str());
}

void test_samples_for_every_roof_event()
{
    const Config cfg = Config::createDefault();
    for (const char *key : ROOF_KEYS)
    {
        const Core::SampleAlert *sample = Core::sampleAlert(key);
        TEST_ASSERT_NOT_NULL_MESSAGE(sample, key);
        TEST_ASSERT_EQUAL_STRING(key, Alerts::alertTypeName(sample->type));
        const Alerts::Alert test = Core::buildTestAlert(sample, {1, "", "", "{reason}"}, Core::AlertSources{cfg}, CLOCK);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(test.message.c_str(), "(example)"), key);
        const int want = Alerts::isCritical(sample->type) ? static_cast<int>(Alerts::AlertLevel::Urgent) : 1;
        TEST_ASSERT_EQUAL_MESSAGE(want, static_cast<int>(test.level), key);
    }
    // A critical sample with wording that drops the reason still carries it.
    const Alerts::Alert parked =
        Core::buildTestAlert(Core::sampleAlert("mount_not_parked"), {4, "", "", "Look"}, Core::AlertSources{cfg}, CLOCK);
    TEST_ASSERT_NOT_NULL(strstr(parked.message.c_str(), "This is how"));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_roof_event_defaults);
    RUN_TEST(test_roof_event_round_trip_and_old_config);
    RUN_TEST(test_critical_level_floor_in_validation);
    RUN_TEST(test_full_roof_templates_fit);
    RUN_TEST(test_settings_documents_are_not_cut_short);
    RUN_TEST(test_critical_level_floor_at_send);
    RUN_TEST(test_critical_built_in_wording_and_sound);
    RUN_TEST(test_critical_custom_wording_keeps_the_reason);
    RUN_TEST(test_critical_reminder_title_keeps_still);
    RUN_TEST(test_rules_follow_the_roof_levels);
    RUN_TEST(test_engine_roof_alerts_get_their_settings);
    RUN_TEST(test_samples_for_every_roof_event);
    return UNITY_END();
}
