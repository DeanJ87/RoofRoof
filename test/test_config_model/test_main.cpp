#include <unity.h>

#include <ArduinoJson.h>

#include <cstring>

#include "Config.h"

using namespace SQM;

namespace
{
    std::string rejectReason(const std::string &json)
    {
        const Config base = Config::createDefault();
        std::string error;
        const auto cfg = Config::fromJson(json, &base, &error);
        return cfg ? std::string() : error;
    }
} // namespace

void setUp() {}
void tearDown() {}

void test_defaults_are_valid_and_round_trip()
{
    const Config defaults = Config::createDefault();
    std::string error;
    TEST_ASSERT_TRUE_MESSAGE(defaults.validate(&error), error.c_str());
    TEST_ASSERT_EQUAL_STRING("roofroof", defaults.wifi.hostname.c_str());
    TEST_ASSERT_TRUE(defaults.wifi.mdns);
    TEST_ASSERT_TRUE(defaults.wifi.ipv6);
    TEST_ASSERT_EQUAL_STRING("roofroof", defaults.mqtt.topic.c_str());
    TEST_ASSERT_EQUAL_STRING("RoofRoof", defaults.deviceName.c_str());

    // A save/load round trip changes nothing.
    const auto loaded = Config::fromJson(defaults.toJson());
    TEST_ASSERT_TRUE(loaded.has_value());
    const auto again = Config::fromJson(loaded->toJson());
    TEST_ASSERT_TRUE(again.has_value());
    TEST_ASSERT_EQUAL_STRING(loaded->toJson().c_str(), again->toJson().c_str());
}

void test_secrets_redacted_and_kept()
{
    Config cfg = Config::createDefault();
    cfg.wifi.ssid = "Home";
    cfg.wifi.password = "hunter22";
    const std::string redacted = cfg.toJson(true);
    TEST_ASSERT_TRUE(redacted.find("hunter22") == std::string::npos);
    TEST_ASSERT_TRUE(redacted.find("********") != std::string::npos);

    // Sending the mask back keeps the stored password.
    const auto updated = Config::fromJson(redacted, &cfg);
    TEST_ASSERT_TRUE(updated.has_value());
    TEST_ASSERT_EQUAL_STRING("hunter22", updated->wifi.password.c_str());
}

void test_partial_json_merges_onto_base()
{
    Config base = Config::createDefault();
    base.deviceName = "Observatory";
    const auto cfg = Config::fromJson("{\"alpaca\":{\"enabled\":true}}", &base);
    TEST_ASSERT_TRUE(cfg.has_value());
    TEST_ASSERT_TRUE(cfg->alpaca.enabled);
    TEST_ASSERT_EQUAL_STRING("Observatory", cfg->deviceName.c_str());
}

void test_rejections_say_why()
{
    TEST_ASSERT_EQUAL_STRING("Device name is required", rejectReason("{\"deviceName\":\"\"}").c_str());
    TEST_ASSERT_TRUE(rejectReason("{\"wifi\":{\"hostname\":\"-bad\"}}").find("Hostname") == 0);
    TEST_ASSERT_TRUE(rejectReason("{\"mqtt\":{\"enabled\":true,\"broker\":\"b\",\"topic\":\"a/#\"}}").find("MQTT topic") == 0);
    TEST_ASSERT_TRUE(rejectReason("{\"ntp\":{\"syncIntervalMs\":599999}}").size() > 0);
    TEST_ASSERT_TRUE(rejectReason("{not json").find("couldn't be read") != std::string::npos);
    TEST_ASSERT_EQUAL_STRING("", rejectReason("{\"ntp\":{\"syncIntervalMs\":600000}}").c_str());
}

// Constraints (SQMeter spec 020): combinations that can never work stay
// rejected with the device's message - D-33 MQTT broker and topic, D-34 HTTP
// auth password. The web UI shows the same
// messages before saving (web/src/__tests__/configSchema.test.ts).
void test_constraints_rejected_with_messages()
{
    struct
    {
        const char *json;
        const char *message;
    } cases[] = {
        {R"({"mqtt":{"enabled":true,"broker":"","topic":"roofroof"}})", "MQTT broker and topic are required when MQTT is enabled"}, // D-33
        {R"({"auth":{"enabled":true,"username":"admin","password":""}})", "HTTP auth password is required when auth is enabled"},   // D-34
    };
    for (const auto &c : cases)
    {
        std::string error;
        TEST_ASSERT_FALSE_MESSAGE(Config::fromJson(c.json, nullptr, &error).has_value(), c.json);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(c.message, error.c_str(), c.json);
    }
}

// D-32 is a dependency, not a constraint: command-line uploads without a
// password are kept (devices in the field store this) and reported inactive.
void test_ota_without_password_is_kept()
{
    auto cfg = Config::fromJson(R"({"ota":{"enabled":true,"password":""}})");
    TEST_ASSERT_TRUE(cfg.has_value());
    TEST_ASSERT_TRUE(cfg->ota.enabled);
}

// Without GPS, NTP may be off: the device then has no clock, which nothing
// in the platform needs (SQMeter's "at least one time source" rule is gone).
void test_ntp_can_be_switched_off()
{
    TEST_ASSERT_EQUAL_STRING("", rejectReason("{\"ntp\":{\"enabled\":false}}").c_str());
    TEST_ASSERT_EQUAL_STRING("", rejectReason("{\"ntp\":{\"syncIntervalMs\":86400000}}").c_str());
    TEST_ASSERT_TRUE(rejectReason("{\"ntp\":{\"syncIntervalMs\":86400001}}").size() > 0);
}

void test_mqtt_interval_and_hostname_boundaries()
{
    TEST_ASSERT_EQUAL_STRING("", rejectReason("{\"mqtt\":{\"publishIntervalMs\":86400000}}").c_str());
    TEST_ASSERT_TRUE(rejectReason("{\"mqtt\":{\"publishIntervalMs\":86400001}}").size() > 0);
    TEST_ASSERT_EQUAL_STRING("", rejectReason("{\"wifi\":{\"hostname\":\"a-32-character-hostname-ok-12345\"}}").c_str());
    TEST_ASSERT_TRUE(rejectReason("{\"wifi\":{\"hostname\":\"a-33-character-hostname-ok-123456\"}}").size() > 0);
}

// --- When to send and the imaging-app events (SQMeter spec 021) ---

void test_alert_schedule_defaults()
{
    const Config cfg = Config::createDefault();
    TEST_ASSERT_TRUE(cfg.alerts.sendMode == AlertsConfig::SendMode::Any);
    TEST_ASSERT_EQUAL_UINT32(120, cfg.alerts.clientSilentSeconds);
    TEST_ASSERT_EQUAL_UINT8(3, cfg.alerts.clientLost.level);
    TEST_ASSERT_EQUAL_UINT8(1, cfg.alerts.clientBack.level);
    TEST_ASSERT_EQUAL_UINT8(0, cfg.alerts.clientDisconnected.level);
    DynamicJsonDocument doc(16384);
    deserializeJson(doc, cfg.toJson());
    TEST_ASSERT_EQUAL_STRING("any", doc["alerts"]["sendMode"]);
    TEST_ASSERT_TRUE(doc["alerts"]["armWithAlpaca"].isNull());
    TEST_ASSERT_EQUAL(3, doc["alerts"]["events"]["client_lost"]["level"].as<int>());
}

void test_send_mode_is_validated_and_stored_with_the_client_part()
{
    const Config base = Config::createDefault();
    auto cfg = Config::fromJson("{\"alerts\":{\"sendMode\":\"whileConnected\"}}", &base);
    TEST_ASSERT_TRUE(cfg.has_value());
    TEST_ASSERT_TRUE(cfg->alerts.sendMode == AlertsConfig::SendMode::WhileConnected);
    DynamicJsonDocument main(4096);
    deserializeJson(main, cfg->alertsToJson(false, Config::AlertsPart::Main));
    TEST_ASSERT_TRUE(main["sendMode"].isNull());
    TEST_ASSERT_TRUE(main["events"]["client_lost"].isNull());
    DynamicJsonDocument client(4096);
    deserializeJson(client, cfg->alertsToJson(false, Config::AlertsPart::Client));
    TEST_ASSERT_EQUAL_STRING("whileConnected", client["sendMode"]);
    TEST_ASSERT_EQUAL_STRING(
        "Alerts: when to send must be any or whileConnected", rejectReason("{\"alerts\":{\"sendMode\":\"sometimes\"}}").c_str());
}

void test_client_silence_boundaries()
{
    TEST_ASSERT_EQUAL_STRING("", rejectReason("{\"alerts\":{\"clientSilentSeconds\":30}}").c_str());
    TEST_ASSERT_EQUAL_STRING("", rejectReason("{\"alerts\":{\"clientSilentSeconds\":3600}}").c_str());
    TEST_ASSERT_EQUAL_STRING(
        "Alerts: silence time must be between 30 and 3600 seconds", rejectReason("{\"alerts\":{\"clientSilentSeconds\":29}}").c_str());
    TEST_ASSERT_TRUE(rejectReason("{\"alerts\":{\"clientSilentSeconds\":3601}}").size() > 0);
}

void test_stored_parts_round_trip()
{
    Config cfg = Config::createDefault();
    cfg.alerts.sendMode = AlertsConfig::SendMode::WhileConnected;
    cfg.alerts.clientSilentSeconds = 90;
    cfg.alerts.clientLost = {4, "siren", "Lost {device}", "Gone for {silent_for}"};
    // As ConfigStore loads them: the main document with both parts spliced in.
    const std::string stored = std::string("{\"alerts\":") + cfg.alertsToJson(false, Config::AlertsPart::Main) +
                               ",\"alertsClient\":" + cfg.alertsToJson(false, Config::AlertsPart::Client) + "}";
    const auto loaded = Config::fromJson(stored);
    TEST_ASSERT_TRUE(loaded.has_value());
    TEST_ASSERT_TRUE(loaded->alerts.sendMode == AlertsConfig::SendMode::WhileConnected);
    TEST_ASSERT_EQUAL_UINT32(90, loaded->alerts.clientSilentSeconds);
    TEST_ASSERT_EQUAL_UINT8(4, loaded->alerts.clientLost.level);
    TEST_ASSERT_EQUAL_STRING("Gone for {silent_for}", loaded->alerts.clientLost.message.c_str());
    // As spliced by Config::spliceStored.
    const auto spliced = Config::fromJson(Config::spliceStored(
        cfg.toJson(false, false), cfg.alertsToJson(false, Config::AlertsPart::Main), cfg.alertsToJson(false, Config::AlertsPart::Client)));
    TEST_ASSERT_TRUE(spliced.has_value());
    TEST_ASSERT_EQUAL_UINT32(90, spliced->alerts.clientSilentSeconds);
}

void test_full_client_templates_fit_and_add_nothing_to_main()
{
    Config cfg = Config::createDefault();
    const std::string mainBefore = cfg.alertsToJson(false, Config::AlertsPart::Main);
    const std::string title(AlertsConfig::MAX_TEMPLATE_TITLE, 'T');
    const std::string message(AlertsConfig::MAX_TEMPLATE_MESSAGE, 'M');
    for (AlertsConfig::EventSetting *event : {&cfg.alerts.clientLost, &cfg.alerts.clientBack, &cfg.alerts.clientDisconnected})
        *event = {4, "persistent", title, message};
    cfg.alerts.sendMode = AlertsConfig::SendMode::WhileConnected;
    std::string error;
    TEST_ASSERT_TRUE_MESSAGE(cfg.validate(&error), error.c_str());
    TEST_ASSERT_TRUE(cfg.alertsToJson(false, Config::AlertsPart::Client).size() <= Config::MAX_ALERTS_JSON_BYTES);
    // The client events and send mode live in their own part only.
    TEST_ASSERT_EQUAL_STRING(mainBefore.c_str(), cfg.alertsToJson(false, Config::AlertsPart::Main).c_str());
}

// The limits count characters, like the web UI: 80 Japanese characters are
// 240 bytes but still fit; 81 don't.
void test_alert_wording_limits_count_characters_not_bytes()
{
    const char *rain = "\xE9\x9B\xA8"; // 雨, 3 bytes in UTF-8
    const char *meem = "\xD9\x85";     // م, 2 bytes in UTF-8
    std::string title;
    for (size_t i = 0; i < AlertsConfig::MAX_TEMPLATE_TITLE; ++i)
        title += rain;
    Config cfg = Config::createDefault();
    cfg.alerts.clientLost.title = title;
    std::string error;
    TEST_ASSERT_TRUE_MESSAGE(cfg.validate(&error), error.c_str());
    TEST_ASSERT_TRUE(AlertsConfig::templateFits(title, ""));
    cfg.alerts.clientLost.title += rain;
    TEST_ASSERT_FALSE(cfg.validate(&error));
    TEST_ASSERT_FALSE(AlertsConfig::templateFits(cfg.alerts.clientLost.title, ""));
    std::string message;
    for (size_t i = 0; i < AlertsConfig::MAX_TEMPLATE_MESSAGE; ++i)
        message += meem;
    cfg.alerts.clientLost.title = "x";
    cfg.alerts.clientLost.message = message;
    TEST_ASSERT_TRUE_MESSAGE(cfg.validate(&error), error.c_str());
}

void test_ipv6_setting_defaults_on_and_round_trips()
{
    const Config base = Config::createDefault();
    // Older saved config has no "ipv6": on.
    const auto old = Config::fromJson(R"({"wifi":{"ssid":"Home","mdns":true}})");
    TEST_ASSERT_TRUE(old.has_value());
    TEST_ASSERT_TRUE(old->wifi.ipv6);
    const auto off = Config::fromJson(R"({"wifi":{"ipv6":false}})", &base);
    TEST_ASSERT_TRUE(off.has_value());
    TEST_ASSERT_FALSE(off->wifi.ipv6);
    const auto again = Config::fromJson(off->toJson());
    TEST_ASSERT_FALSE(again->wifi.ipv6);
}

void test_ipv6_broker_and_webhook_forms()
{
    // Accepted: bare and bracketed IPv6, bracketed with a port, http with a bracketed IPv6.
    TEST_ASSERT_EQUAL_STRING("", rejectReason(R"({"mqtt":{"enabled":true,"broker":"fd00::10","topic":"roof"}})").c_str());
    TEST_ASSERT_EQUAL_STRING("", rejectReason(R"({"mqtt":{"enabled":true,"broker":"[fd00::10]:1883","topic":"roof"}})").c_str());
    TEST_ASSERT_EQUAL_STRING("", rejectReason(R"({"alerts":{"webhook":{"enabled":true,"url":"http://[fd00::10]:8080/hook"}}})").c_str());

    TEST_ASSERT_EQUAL_STRING(
        "MQTT broker: Put the port in the Port field",
        rejectReason(R"({"mqtt":{"enabled":true,"broker":"broker.local:1883","topic":"roof"}})").c_str());
    TEST_ASSERT_EQUAL_STRING(
        "MQTT broker: Not a valid IPv6 address", rejectReason(R"({"mqtt":{"enabled":true,"broker":"fd00::zz","topic":"roof"}})").c_str());
    TEST_ASSERT_EQUAL_STRING(
        "Alerts: webhook URL: https to an IPv6 address isn't supported yet - use a host name, or http",
        rejectReason(R"({"alerts":{"webhook":{"enabled":true,"url":"https://[fd00::10]/hook"}}})").c_str());

    TEST_ASSERT_EQUAL_STRING(
        "Alerts: ntfy server: https to an IPv6 address isn't supported yet - use a host name, or http",
        rejectReason(R"({"alerts":{"ntfy":{"enabled":true,"server":"https://[fd00::10]","topic":"t"}}})").c_str());
    TEST_ASSERT_EQUAL_STRING(
        "", rejectReason(R"({"alerts":{"ntfy":{"enabled":true,"server":"http://[fd00::10]:8080","topic":"t"}}})").c_str());
    TEST_ASSERT_EQUAL_STRING(
        "NTP server: NTP over IPv6 isn't supported yet - use a host name or an IPv4 address",
        rejectReason(R"({"ntp":{"server2":"2001:db8::123"}})").c_str());

    // Saved config isn't held to the new forms, so it always loads.
    const auto stored = Config::fromJson(R"({"mqtt":{"enabled":true,"broker":"broker.local:1883","topic":"roof"}})");
    TEST_ASSERT_TRUE(stored.has_value());
}

// A settings save never takes the device off its network (SQMeter spec 011 FR-009):
// an empty WiFi network is refused while one is set; WiFi setup changes it.
void test_masked_mqtt_password_round_trips_through_a_save()
{
    // Settings out masks the password; saving the form back unchanged must keep it.
    Config base = Config::createDefault();
    base.mqtt.broker = "broker.local";
    base.mqtt.username = "roof";
    base.mqtt.password = "real-secret";
    const std::string shown = base.toJson(true);
    TEST_ASSERT_TRUE(shown.find("real-secret") == std::string::npos);
    std::string error;
    const auto saved = Config::fromJson(shown, &base, &error);
    TEST_ASSERT_TRUE_MESSAGE(saved.has_value(), error.c_str());
    TEST_ASSERT_EQUAL_STRING("real-secret", saved->mqtt.password.c_str());
    const auto again = Config::fromJson(saved->toJson(true), &*saved, &error);
    TEST_ASSERT_TRUE_MESSAGE(again.has_value(), error.c_str());
    TEST_ASSERT_EQUAL_STRING("real-secret", again->mqtt.password.c_str());
}

void test_mqtt_test_uses_the_saved_password_only_for_the_saved_broker()
{
    MQTTConfig saved = Config::createDefault().mqtt;
    saved.broker = "broker.local";
    saved.port = 1883;
    saved.username = "roof";
    saved.password = "real-secret";
    const auto tryWith = [&](const char *broker, uint16_t port, const char *user, const char *password)
    { return Config::mqttTestPassword(saved, {broker, port, user, password}); };

    // The mask (or a blank field) for the saved broker tests with the saved password.
    TEST_ASSERT_EQUAL_STRING("real-secret", tryWith("broker.local", 1883, "roof", "********").value().c_str());
    TEST_ASSERT_EQUAL_STRING("real-secret", tryWith("broker.local", 1883, "roof", "").value().c_str());

    // A typed password is always used as typed.
    TEST_ASSERT_EQUAL_STRING("typed", tryWith("other.host", 1883, "roof", "typed").value().c_str());

    // The mask for another broker, port or user never sends the saved password.
    TEST_ASSERT_FALSE(tryWith("evil.host", 1883, "roof", "********").has_value());
    TEST_ASSERT_FALSE(tryWith("broker.local", 8883, "roof", "********").has_value());
    TEST_ASSERT_FALSE(tryWith("broker.local", 1883, "admin", "********").has_value());

    // A blank password for another broker is a test without a password, not a refusal.
    TEST_ASSERT_EQUAL_STRING("", tryWith("other.host", 1883, "", "").value().c_str());

    // Nothing saved: what was sent is what's tried.
    saved.password.clear();
    TEST_ASSERT_EQUAL_STRING("********", tryWith("evil.host", 1883, "roof", "********").value().c_str());
}

void test_settings_save_cannot_blank_the_wifi_network()
{
    Config base = Config::createDefault();
    base.wifi.ssid = "HomeNet";
    base.wifi.password = "secret-wifi";
    std::string error;
    TEST_ASSERT_FALSE(Config::fromJson("{\"wifi\":{\"ssid\":\"\"}}", &base, &error).has_value());
    TEST_ASSERT_EQUAL_STRING("WiFi network can't be blank; use WiFi setup to change networks", error.c_str());
    TEST_ASSERT_FALSE(Config::fromJson("{\"wifi\":{\"ssid\":\"  \"}}", &base, &error).has_value());

    // A missing ssid keeps the network; another network is still allowed.
    const auto kept = Config::fromJson("{\"wifi\":{\"hostname\":\"garden\"}}", &base, &error);
    TEST_ASSERT_TRUE_MESSAGE(kept.has_value(), error.c_str());
    TEST_ASSERT_EQUAL_STRING("HomeNet", kept->wifi.ssid.c_str());
    TEST_ASSERT_EQUAL_STRING("secret-wifi", kept->wifi.password.c_str());
    const auto moved = Config::fromJson("{\"wifi\":{\"ssid\":\"Observatory\"}}", &base, &error);
    TEST_ASSERT_TRUE_MESSAGE(moved.has_value(), error.c_str());
    TEST_ASSERT_EQUAL_STRING("Observatory", moved->wifi.ssid.c_str());

    // With no network yet (setup hotspot) other settings still save.
    const Config fresh = Config::createDefault();
    TEST_ASSERT_TRUE(Config::fromJson("{\"wifi\":{\"ssid\":\"\"},\"deviceName\":\"Roof\"}", &fresh).has_value());
}

// A wrong-typed value is refused, naming the field, never swapped for a
// default (the old `value | fallback` turned a null ssid into "").
void test_wrong_types_are_rejected_not_defaulted()
{
    Config base = Config::createDefault();
    base.wifi.ssid = "HomeNet";
    base.deviceName = "Observatory";
    std::string error;
    auto reason = [&](const char *json)
    {
        error.clear();
        return Config::fromJson(json, &base, &error).has_value() ? std::string("accepted") : error;
    };
    TEST_ASSERT_EQUAL_STRING("Settings: wifi.ssid must be a string", reason("{\"wifi\":{\"ssid\":null}}").c_str());
    TEST_ASSERT_EQUAL_STRING("Settings: wifi.ssid must be a string", reason("{\"wifi\":{\"ssid\":42}}").c_str());
    TEST_ASSERT_EQUAL_STRING("Settings: deviceName must be a string", reason("{\"deviceName\":7}").c_str());
    TEST_ASSERT_EQUAL_STRING("Settings: deviceName must be a string", reason("{\"deviceName\":null}").c_str());
    TEST_ASSERT_EQUAL_STRING("Settings: wifi.mdns must be true or false", reason("{\"wifi\":{\"mdns\":\"yes\"}}").c_str());
    TEST_ASSERT_EQUAL_STRING("mqtt.port must be a whole number in range", reason("{\"mqtt\":{\"port\":70000}}").c_str());
    TEST_ASSERT_EQUAL_STRING("Settings: ota.enabled must be true or false", reason("{\"ota\":{\"enabled\":\"yes\"}}").c_str());
    TEST_ASSERT_EQUAL_STRING(
        "Settings: mqtt.homeAssistant.enabled must be true or false", reason("{\"mqtt\":{\"homeAssistant\":{\"enabled\":1}}}").c_str());
    TEST_ASSERT_EQUAL_STRING(
        "Settings: alerts.events.client_lost.level must be a whole number in range",
        reason("{\"alerts\":{\"events\":{\"client_lost\":{\"level\":\"4\"}}}}").c_str());
    TEST_ASSERT_EQUAL_STRING("Settings: mqtt.password must be a string", reason("{\"mqtt\":{\"password\":123}}").c_str());
    TEST_ASSERT_EQUAL_STRING("Settings: alpaca.enabled must be true or false", reason("{\"alpaca\":{\"enabled\":\"on\"}}").c_str());
    TEST_ASSERT_EQUAL_STRING("Settings: wifi must be an object", reason("{\"wifi\":\"HomeNet\"}").c_str());
    TEST_ASSERT_EQUAL_STRING("Settings: wifi must be an object", reason("{\"wifi\":null}").c_str());
    TEST_ASSERT_EQUAL_STRING("Settings must be a JSON object", reason("[]").c_str());

    // Null still clears a secret, as documented.
    base.mqtt.password = "broker-secret";
    const auto cleared = Config::fromJson("{\"mqtt\":{\"password\":null}}", &base, &error);
    TEST_ASSERT_TRUE_MESSAGE(cleared.has_value(), error.c_str());
    TEST_ASSERT_EQUAL_STRING("", cleared->mqtt.password.c_str());
}

// Stored settings keep loading: a wrong-typed stored value keeps the current
// value rather than failing the whole load (which would drop the device into
// its setup hotspot) or turning into a hard-coded default.
void test_stored_wrong_type_keeps_current_value()
{
    Config cfg = Config::createDefault();
    std::string error;
    TEST_ASSERT_TRUE_MESSAGE(
        Config::applyJson("{\"deviceName\":5,\"wifi\":{\"ssid\":\"HomeNet\",\"hostname\":null}}", cfg, false, &error), error.c_str());
    TEST_ASSERT_EQUAL_STRING("HomeNet", cfg.wifi.ssid.c_str());
    TEST_ASSERT_EQUAL_STRING(Config::createDefault().deviceName.c_str(), cfg.deviceName.c_str());
    TEST_ASSERT_EQUAL_STRING("roofroof", cfg.wifi.hostname.c_str());
}

namespace
{
    std::string roofReason(const std::string &roofJson)
    {
        return rejectReason("{\"roof\":" + roofJson + "}");
    }
} // namespace

void test_roof_every_field_round_trips()
{
    Config cfg = Config::createDefault();
    cfg.roof.travelMm = 4200;
    cfg.roof.speedMmPerSecond = 61.5f;
    cfg.roof.timeoutMarginSeconds = 44;
    cfg.roof.leaveMagnetSeconds = 33;
    cfg.roof.rampUpMs = 1234;
    cfg.roof.rampDownMs = 4321;
    cfg.roof.topDutyPercent = 90;
    cfg.roof.slowDutyPercent = 35;
    cfg.roof.openDirection = RoofConfig::Direction::Reverse;
    cfg.roof.hallHighEnd = RoofConfig::HallEnd::Closed;
    cfg.roof.hallHighCounts = 2800;
    cfg.roof.hallLowCounts = 900;
    cfg.roof.currentSenseEnabled = true;
    cfg.roof.maxCurrentAmps = 12.5f;
    cfg.roof.currentAmpsPerVolt = 9.25f;
    cfg.roof.minRunningAmps = 1.5f;
    cfg.roof.minSupplyVolts = 10.5f;
    const std::string json = cfg.toJson(false);
    TEST_ASSERT_TRUE(json.find("\"openDirection\":\"reverse\"") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"hallHighEnd\":\"closed\"") != std::string::npos);
    const auto loaded = Config::fromJson(json);
    TEST_ASSERT_TRUE(loaded.has_value());
    const RoofConfig &r = loaded->roof;
    TEST_ASSERT_EQUAL_UINT32(4200, r.travelMm);
    TEST_ASSERT_EQUAL_FLOAT(61.5f, r.speedMmPerSecond);
    TEST_ASSERT_EQUAL_UINT32(44, r.timeoutMarginSeconds);
    TEST_ASSERT_EQUAL_UINT32(33, r.leaveMagnetSeconds);
    TEST_ASSERT_EQUAL_UINT32(1234, r.rampUpMs);
    TEST_ASSERT_EQUAL_UINT32(4321, r.rampDownMs);
    TEST_ASSERT_EQUAL_UINT32(90, r.topDutyPercent);
    TEST_ASSERT_EQUAL_UINT32(35, r.slowDutyPercent);
    TEST_ASSERT_TRUE(r.openDirection == RoofConfig::Direction::Reverse);
    TEST_ASSERT_TRUE(r.hallHighEnd == RoofConfig::HallEnd::Closed);
    TEST_ASSERT_EQUAL_UINT32(2800, r.hallHighCounts);
    TEST_ASSERT_EQUAL_UINT32(900, r.hallLowCounts);
    TEST_ASSERT_TRUE(r.currentSenseEnabled);
    TEST_ASSERT_EQUAL_FLOAT(12.5f, r.maxCurrentAmps);
    TEST_ASSERT_EQUAL_FLOAT(9.25f, r.currentAmpsPerVolt);
    TEST_ASSERT_EQUAL_FLOAT(1.5f, r.minRunningAmps);
    TEST_ASSERT_EQUAL_FLOAT(10.5f, r.minSupplyVolts);
    TEST_ASSERT_EQUAL_STRING(json.c_str(), loaded->toJson(false).c_str());
}

void test_roof_defaults_match_the_spec_and_are_valid()
{
    const RoofConfig r = Config::createDefault().roof;
    TEST_ASSERT_EQUAL_UINT32(1850, r.travelMm);
    TEST_ASSERT_EQUAL_UINT32(40, r.slowDutyPercent);
    TEST_ASSERT_FALSE(r.currentSenseEnabled);
    TEST_ASSERT_TRUE(r.openDirection == RoofConfig::Direction::Forward);
    TEST_ASSERT_TRUE(r.hallHighEnd == RoofConfig::HallEnd::Open);
}

void test_roof_bad_enum_string_is_rejected()
{
    TEST_ASSERT_EQUAL_STRING("Roof: open direction must be forward or reverse", roofReason("{\"openDirection\":\"sideways\"}").c_str());
    TEST_ASSERT_EQUAL_STRING("Roof: Hall high end must be open or closed", roofReason("{\"hallHighEnd\":\"middle\"}").c_str());
    TEST_ASSERT_EQUAL_STRING("Roof: open direction must be forward or reverse", roofReason("{\"openDirection\":1}").c_str());
}

void test_roof_slow_duty_above_top_duty_is_rejected()
{
    const char *message = "Roof: slow duty can't be above the top duty";
    TEST_ASSERT_EQUAL_STRING(message, roofReason("{\"topDutyPercent\":50,\"slowDutyPercent\":51}").c_str());
    TEST_ASSERT_EQUAL_STRING("", roofReason("{\"topDutyPercent\":50,\"slowDutyPercent\":50}").c_str());
}

void test_roof_hall_gap_of_80_is_accepted_and_79_rejected()
{
    const char *message = "Roof: the Hall high threshold must be at least 80 counts above the low threshold";
    TEST_ASSERT_EQUAL_STRING("", roofReason("{\"hallHighCounts\":1180,\"hallLowCounts\":1100}").c_str());
    TEST_ASSERT_EQUAL_STRING(message, roofReason("{\"hallHighCounts\":1179,\"hallLowCounts\":1100}").c_str());
}

void test_roof_out_of_range_says_which_setting()
{
    TEST_ASSERT_EQUAL_STRING("Roof: travel must be between 100 and 20000 mm", roofReason("{\"travelMm\":99}").c_str());
    TEST_ASSERT_EQUAL_STRING("Roof: speed must be between 1 and 500 mm/s", roofReason("{\"speedMmPerSecond\":501}").c_str());
    TEST_ASSERT_EQUAL_STRING("Roof: ramp down must be between 0 and 30000 ms", roofReason("{\"rampDownMs\":30001}").c_str());
}

void test_roof_at_every_maximum_fits_the_persisted_size()
{
    Config cfg = Config::createDefault();
    cfg.roof.travelMm = 20000;
    cfg.roof.speedMmPerSecond = 500.0f;
    cfg.roof.timeoutMarginSeconds = 300;
    cfg.roof.leaveMagnetSeconds = 120;
    cfg.roof.rampUpMs = 30000;
    cfg.roof.rampDownMs = 30000;
    cfg.roof.topDutyPercent = 100;
    cfg.roof.slowDutyPercent = 100;
    cfg.roof.openDirection = RoofConfig::Direction::Reverse;
    cfg.roof.hallHighEnd = RoofConfig::HallEnd::Closed;
    cfg.roof.hallHighCounts = 3995;
    cfg.roof.hallLowCounts = 3915;
    cfg.roof.currentSenseEnabled = true;
    cfg.roof.maxCurrentAmps = 40.0f;
    cfg.roof.minSupplyVolts = 15.0f;
    std::string error;
    TEST_ASSERT_TRUE_MESSAGE(cfg.validate(&error), error.c_str());
    TEST_ASSERT_TRUE(cfg.toJson(false, false).size() < Config::MAX_PERSISTED_JSON_BYTES);
    // ArduinoJson drops what doesn't fit without an error: with long strings
    // everywhere, every section must still be in the document.
    cfg.wifi.ssid = std::string(32, 's');
    cfg.wifi.password = std::string(63, 'p');
    cfg.mqtt.broker = std::string(60, 'b');
    cfg.safety.httpUrl = "http://" + std::string(100, 'h') + "/api/safety";
    cfg.park.ninaUrl = "http://" + std::string(100, 'n') + ":1888";
    const std::string json = cfg.toJson();
    for (const char *section : {"\"wifi\"", "\"mqtt\"", "\"safety\"", "\"park\"", "\"roof\"", "\"alerts\"", "\"minSupplyVolts\""})
        TEST_ASSERT_TRUE_MESSAGE(json.find(section) != std::string::npos, section);
}

void test_roof_missing_section_keeps_defaults()
{
    const auto cfg = Config::fromJson("{\"deviceName\":\"x\"}");
    TEST_ASSERT_TRUE(cfg.has_value());
    TEST_ASSERT_EQUAL_UINT32(1850, cfg->roof.travelMm);
    TEST_ASSERT_FALSE(cfg->roof.currentSenseEnabled);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults_are_valid_and_round_trip);
    RUN_TEST(test_secrets_redacted_and_kept);
    RUN_TEST(test_partial_json_merges_onto_base);
    RUN_TEST(test_rejections_say_why);
    RUN_TEST(test_ntp_can_be_switched_off);
    RUN_TEST(test_constraints_rejected_with_messages);
    RUN_TEST(test_ota_without_password_is_kept);
    RUN_TEST(test_mqtt_interval_and_hostname_boundaries);
    RUN_TEST(test_alert_schedule_defaults);
    RUN_TEST(test_send_mode_is_validated_and_stored_with_the_client_part);
    RUN_TEST(test_client_silence_boundaries);
    RUN_TEST(test_stored_parts_round_trip);
    RUN_TEST(test_full_client_templates_fit_and_add_nothing_to_main);
    RUN_TEST(test_alert_wording_limits_count_characters_not_bytes);
    RUN_TEST(test_ipv6_setting_defaults_on_and_round_trips);
    RUN_TEST(test_ipv6_broker_and_webhook_forms);
    RUN_TEST(test_settings_save_cannot_blank_the_wifi_network);
    RUN_TEST(test_masked_mqtt_password_round_trips_through_a_save);
    RUN_TEST(test_mqtt_test_uses_the_saved_password_only_for_the_saved_broker);
    RUN_TEST(test_wrong_types_are_rejected_not_defaulted);
    RUN_TEST(test_stored_wrong_type_keeps_current_value);
    RUN_TEST(test_roof_every_field_round_trips);
    RUN_TEST(test_roof_defaults_match_the_spec_and_are_valid);
    RUN_TEST(test_roof_bad_enum_string_is_rejected);
    RUN_TEST(test_roof_slow_duty_above_top_duty_is_rejected);
    RUN_TEST(test_roof_hall_gap_of_80_is_accepted_and_79_rejected);
    RUN_TEST(test_roof_out_of_range_says_which_setting);
    RUN_TEST(test_roof_at_every_maximum_fits_the_persisted_size);
    RUN_TEST(test_roof_missing_section_keeps_defaults);
    return UNITY_END();
}
