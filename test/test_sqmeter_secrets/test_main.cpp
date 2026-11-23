// Spec 024: SQMeter's secrets (GET /api/secrets) and on/off switches. The
// read of the secrets and each refusal, the copy's document (switches and the
// secrets part, never a value), the placeholder a save swaps for the held
// value (only for the server it came with), and the sync (secrets filled for
// SQMeter's server, never cleared; switches only turned on).

#include <unity.h>

#include <ArduinoJson.h>

#include <string>

#include "Config.h"
#include "SqmeterSecrets.h"
#include "SqmeterSync.h"

using namespace SQM;
using namespace SQM::Site;

void setUp() {}
void tearDown() {}

namespace
{
    constexpr int64_t NOW = 1763540000;
    const char *const PUSHOVER_USER = "uQiRzpo4DXghDmr9QzzfQu27cmVRsG";
    const char *const PUSHOVER_APP = "azGDORePK8gMaC0QOYAMyEEuzJnyUi";

    SecretsRead readOf(int status, const char *body, bool credentials = true)
    {
        DynamicJsonDocument filter(SECRETS_FILTER_BYTES);
        secretsFilter(filter);
        DynamicJsonDocument doc(SECRETS_DOC_BYTES);
        Fetch fetch;
        fetch.status = status;
        fetch.credentials = credentials;
        if (body != nullptr)
            fetch.parsed = !deserializeJson(doc, body, DeserializationOption::Filter(filter));
        return readSecrets(fetch, doc, 1000);
    }

    const char *const SHARED = R"({"mqtt":{"password":"broker-pass"},"alerts":{"ntfy":{"token":"tk_abc"},)"
                               R"("pushover":{"userKey":"uQiRzpo4DXghDmr9QzzfQu27cmVRsG","appToken":"azGDORePK8gMaC0QOYAMyEEuzJnyUi"}}})";

    SiteConfig sqmeter()
    {
        SiteConfig s;
        s.name = "SQMeter";
        s.hasMqtt = true;
        s.broker = "192.168.1.10";
        s.port = 1883;
        s.username = "obs";
        s.topic = "sqmeter";
        s.passwordSet = true;
        s.mqttEnabled = true;
        s.hasNtfy = true;
        s.ntfyServer = "https://ntfy.sh";
        s.ntfyTopic = "obs-alerts";
        s.ntfyTokenSet = true;
        s.ntfyEnabled = true;
        s.hasPushover = true;
        s.pushoverUserKeySet = true;
        s.pushoverAppTokenSet = true;
        s.pushoverEnabled = true;
        return s;
    }

    SiteConfig withSecrets(SiteConfig s)
    {
        s.secrets = readOf(200, SHARED).secrets;
        return s;
    }

    Config following()
    {
        Config c = Config::createDefault();
        c.sqmeterSync.mqtt.follow = true;
        c.sqmeterSync.alerts.follow = true;
        c.sqmeterSync.time.follow = false;
        return c;
    }

    SyncOutcome sync(const Config &live, const SiteConfig &site)
    {
        SyncOutcome out;
        syncFromSite(out, live, site, NOW, true);
        return out;
    }

    CopiedSecrets held()
    {
        SiteConfig site = withSecrets(sqmeter());
        return copiedFrom(site);
    }

    // A save from the UI with the copy's held secrets (nullptr: none held).
    std::string saveError(const std::string &json, Config &cfg, const CopiedSecrets *copied)
    {
        std::string error;
        Config::applyJson(json, cfg, true, &error, copied);
        return error;
    }
} // namespace

// --- the read ---

void test_shared_secrets_are_read(void)
{
    const SecretsRead r = readOf(200, SHARED);
    TEST_ASSERT_TRUE(r.good());
    TEST_ASSERT_TRUE(r.secrets.received);
    TEST_ASSERT_EQUAL_STRING("broker-pass", r.secrets.mqttPassword.c_str());
    TEST_ASSERT_EQUAL_STRING("tk_abc", r.secrets.ntfyToken.c_str());
    TEST_ASSERT_EQUAL_STRING(PUSHOVER_USER, r.secrets.pushoverUserKey.c_str());
    TEST_ASSERT_EQUAL_STRING(PUSHOVER_APP, r.secrets.pushoverAppToken.c_str());
    TEST_ASSERT_EQUAL_STRING("received", secretsProblemName(r.problem));
}

void test_empty_values_mean_not_set(void)
{
    const SecretsRead r = readOf(200, R"({"mqtt":{"password":""},"alerts":{"ntfy":{"token":""},"pushover":{"userKey":"","appToken":""}}})");
    TEST_ASSERT_TRUE(r.good());
    TEST_ASSERT_TRUE(r.secrets.mqttPassword.empty());
    TEST_ASSERT_TRUE(r.secrets.pushoverUserKey.empty());
}

void test_each_refusal_is_told_apart(void)
{
    TEST_ASSERT_EQUAL_STRING("notSupported", secretsProblemName(readOf(404, nullptr).problem));
    TEST_ASSERT_EQUAL_STRING("sharingOff", secretsProblemName(readOf(403, R"({"error":"Password sharing is off in SQMeter"})").problem));
    TEST_ASSERT_EQUAL_STRING(
        "noWebPassword", secretsProblemName(readOf(403, R"({"error":"Set a web password in SQMeter before sharing passwords"})").problem));
    TEST_ASSERT_EQUAL_STRING("loginRefused", secretsProblemName(readOf(401, nullptr, true).problem));
    TEST_ASSERT_EQUAL_STRING("loginNeeded", secretsProblemName(readOf(401, nullptr, false).problem));
    TEST_ASSERT_EQUAL_STRING("loginRefused", secretsProblemName(readOf(403, R"({"error":"Forbidden"})", true).problem));
    TEST_ASSERT_EQUAL_STRING("unreadable", secretsProblemName(readOf(500, nullptr).problem));
    TEST_ASSERT_EQUAL_STRING("unreadable", secretsProblemName(readOf(200, R"({"deviceName":"x"})").problem));
    TEST_ASSERT_EQUAL_STRING("unreadable", secretsProblemName(readOf(200, "not json").problem));
    Fetch none;
    none.error = "connection refused";
    DynamicJsonDocument doc(64);
    const SecretsRead r = readSecrets(none, doc, 5);
    TEST_ASSERT_EQUAL_STRING("unreachable", secretsProblemName(r.problem));
    TEST_ASSERT_EQUAL_STRING("connection refused", r.detail.c_str());
    TEST_ASSERT_FALSE(r.secrets.received);
}

void test_only_the_saved_address_gets_secrets(void)
{
    SafetyConfig saved;
    saved.httpUrl = "http://192.168.1.50";
    TEST_ASSERT_TRUE(savedAddress("192.168.1.50", saved));
    TEST_ASSERT_TRUE(savedAddress("http://192.168.1.50:80/api/safety", saved));
    TEST_ASSERT_FALSE(savedAddress("http://192.168.1.51", saved));
    TEST_ASSERT_FALSE(savedAddress("https://192.168.1.50", saved));
    TEST_ASSERT_FALSE(savedAddress("http://192.168.1.50:8080", saved));
    saved.httpUrl.clear();
    TEST_ASSERT_FALSE(savedAddress("", saved));
}

// --- the copy's document ---

void test_the_copy_document_never_carries_a_value(void)
{
    SiteConfig site = sqmeter();
    SecretsRead read = readOf(200, SHARED);
    site.secrets = read.secrets;
    Config saved = Config::createDefault();
    saved.alerts.ntfyToken = "tk_abc"; // the same as SQMeter's
    DynamicJsonDocument out(2048);
    writeSiteDocument(out.to<JsonObject>(), "http://192.168.1.50", site);
    writeSecretsPart(out.as<JsonObject>(), read, secretStates(site.secrets, saved), "http://192.168.1.50");
    std::string text;
    serializeJson(out, text);
    TEST_ASSERT_TRUE(text.find("broker-pass") == std::string::npos);
    TEST_ASSERT_TRUE(text.find("tk_abc") == std::string::npos);
    TEST_ASSERT_TRUE(text.find(PUSHOVER_USER) == std::string::npos);
    TEST_ASSERT_TRUE(out["mqtt"]["enabled"].as<bool>());
    TEST_ASSERT_TRUE(out["ntfy"]["enabled"].as<bool>());
    TEST_ASSERT_TRUE(out["pushover"]["enabled"].as<bool>());
    TEST_ASSERT_TRUE(out["secrets"]["problem"].isNull());
    TEST_ASSERT_TRUE(out["secrets"]["http"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("received", out["secrets"]["mqttPassword"].as<const char *>());
    TEST_ASSERT_EQUAL_STRING("same", out["secrets"]["ntfyToken"].as<const char *>());
    TEST_ASSERT_EQUAL_STRING("received", out["secrets"]["pushover"].as<const char *>());
}

void test_the_copy_document_says_why_not(void)
{
    DynamicJsonDocument out(512);
    const SecretsRead read = readOf(403, R"({"error":"Password sharing is off in SQMeter"})");
    writeSecretsPart(out.to<JsonObject>(), read, secretStates(read.secrets, Config::createDefault()), "https://sqm.local");
    TEST_ASSERT_EQUAL_STRING("sharingOff", out["secrets"]["problem"].as<const char *>());
    TEST_ASSERT_FALSE(out["secrets"]["http"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("none", out["secrets"]["mqttPassword"].as<const char *>());
    TEST_ASSERT_EQUAL_STRING("none", out["secrets"]["pushover"].as<const char *>());
}

void test_pushover_needs_both_keys(void)
{
    SiteSecrets s;
    s.received = true;
    s.pushoverUserKey = PUSHOVER_USER;
    TEST_ASSERT_EQUAL(SecretState::None, secretStates(s, Config::createDefault()).pushover);
}

// --- the save swaps the placeholder ---

void test_a_copied_password_is_swapped_in_for_its_broker(void)
{
    Config cfg = Config::createDefault();
    const CopiedSecrets copied = held();
    const std::string error = saveError(
        R"j({"mqtt":{"enabled":true,"broker":"192.168.1.10","port":1883,"username":"obs","password":"(from SQMeter)"}})j", cfg, &copied);
    TEST_ASSERT_EQUAL_STRING("", error.c_str());
    TEST_ASSERT_EQUAL_STRING("broker-pass", cfg.mqtt.password.c_str());
    TEST_ASSERT_TRUE(cfg.mqtt.enabled);
    // Never shown: GET /api/config masks it.
    TEST_ASSERT_TRUE(cfg.toJson(true).find("broker-pass") == std::string::npos);
}

void test_a_copied_password_never_goes_to_another_broker(void)
{
    const CopiedSecrets copied = held();
    Config cfg = Config::createDefault();
    std::string error =
        saveError(R"j({"mqtt":{"broker":"10.0.0.9","port":1883,"username":"obs","password":"(from SQMeter)"}})j", cfg, &copied);
    TEST_ASSERT_EQUAL_STRING(
        "The MQTT password from SQMeter is for SQMeter's own broker, not this one: copy again, or type it", error.c_str());
    cfg = Config::createDefault();
    error = saveError(R"j({"mqtt":{"broker":"192.168.1.10","port":1884,"username":"obs","password":"(from SQMeter)"}})j", cfg, &copied);
    TEST_ASSERT_FALSE(error.empty());
    cfg = Config::createDefault();
    error = saveError(R"j({"alerts":{"ntfy":{"server":"https://ntfy.example","token":"(from SQMeter)"}}})j", cfg, &copied);
    TEST_ASSERT_EQUAL_STRING(
        "The ntfy token from SQMeter is for SQMeter's own ntfy server, not this one: copy again, or type it", error.c_str());
}

void test_without_a_held_copy_the_placeholder_is_refused(void)
{
    Config cfg = Config::createDefault();
    const std::string error =
        saveError(R"j({"mqtt":{"broker":"192.168.1.10","port":1883,"username":"obs","password":"(from SQMeter)"}})j", cfg, nullptr);
    TEST_ASSERT_EQUAL_STRING(
        "A password from SQMeter is no longer held (copied over 30 minutes ago, or the device restarted): copy again, or type it",
        error.c_str());
    // A restore, or any other parse from the UI/API, never holds one.
    TEST_ASSERT_FALSE(Config::fromJson(R"j({"alerts":{"pushover":{"userKey":"(from SQMeter)"}}})j", &cfg).has_value());
}

void test_the_placeholder_elsewhere_is_refused(void)
{
    const CopiedSecrets copied = held();
    Config cfg = Config::createDefault();
    TEST_ASSERT_EQUAL_STRING(
        "Only the MQTT password, the ntfy token and the Pushover keys can come from SQMeter",
        saveError(R"j({"auth":{"password":"(from SQMeter)"}})j", cfg, &copied).c_str());
}

void test_an_empty_held_secret_is_refused(void)
{
    CopiedSecrets copied = held();
    copied.ntfyToken.clear();
    Config cfg = Config::createDefault();
    const std::string error = saveError(R"j({"alerts":{"ntfy":{"server":"https://ntfy.sh","token":"(from SQMeter)"}}})j", cfg, &copied);
    TEST_ASSERT_EQUAL_STRING("SQMeter has no ntfy token to copy: type it", error.c_str());
}

void test_pushover_keys_and_switch_from_the_copy(void)
{
    const CopiedSecrets copied = held();
    Config cfg = Config::createDefault();
    const std::string error =
        saveError(R"j({"alerts":{"pushover":{"enabled":true,"userKey":"(from SQMeter)","appToken":"(from SQMeter)"}}})j", cfg, &copied);
    TEST_ASSERT_EQUAL_STRING("", error.c_str());
    TEST_ASSERT_EQUAL_STRING(PUSHOVER_USER, cfg.alerts.pushoverUserKey.c_str());
    TEST_ASSERT_TRUE(cfg.alerts.pushoverEnabled);
}

void test_stored_settings_never_fill(void)
{
    // A stored placeholder (it can't be stored, but if found) is not a held secret.
    Config cfg = Config::createDefault();
    TEST_ASSERT_TRUE(Config::applyJson(R"j({"mqtt":{"password":"(from SQMeter)"}})j", cfg, false));
    TEST_ASSERT_EQUAL_STRING("(from SQMeter)", cfg.mqtt.password.c_str());
}

// --- the sync ---

void test_sync_fills_secrets_and_turns_switches_on(void)
{
    const SyncOutcome out = sync(following(), withSecrets(sqmeter()));
    TEST_ASSERT_TRUE(out.groups[1].refused.empty());
    TEST_ASSERT_TRUE(out.groups[2].refused.empty());
    TEST_ASSERT_TRUE(out.groups[1].changed);
    TEST_ASSERT_TRUE(out.groups[2].changed);
    TEST_ASSERT_EQUAL_STRING("broker-pass", out.next.mqtt.password.c_str());
    TEST_ASSERT_TRUE(out.next.mqtt.enabled);
    TEST_ASSERT_EQUAL_STRING("tk_abc", out.next.alerts.ntfyToken.c_str());
    TEST_ASSERT_TRUE(out.next.alerts.ntfyEnabled);
    TEST_ASSERT_EQUAL_STRING(PUSHOVER_USER, out.next.alerts.pushoverUserKey.c_str());
    TEST_ASSERT_TRUE(out.next.alerts.pushoverEnabled);
    Config live = following();
    applyOutcome(out, live);
    TEST_ASSERT_EQUAL_STRING("broker-pass", live.mqtt.password.c_str());
    TEST_ASSERT_TRUE(live.mqtt.enabled);
    TEST_ASSERT_TRUE(live.alerts.pushoverEnabled);
    TEST_ASSERT_EQUAL_STRING(PUSHOVER_APP, live.alerts.pushoverAppToken.c_str());
}

void test_sync_never_clears_and_never_turns_off(void)
{
    Config live = following();
    live.mqtt.broker = "192.168.1.10";
    live.mqtt.username = "obs";
    live.mqtt.password = "typed-here";
    live.mqtt.enabled = true;
    live.alerts.pushoverUserKey = PUSHOVER_USER;
    live.alerts.pushoverAppToken = PUSHOVER_APP;
    live.alerts.pushoverEnabled = true;
    SiteConfig site = sqmeter();
    site.mqttEnabled = false;
    site.pushoverEnabled = false;
    site.secrets.received = true; // shared, but SQMeter has none set
    const SyncOutcome out = sync(live, site);
    TEST_ASSERT_EQUAL_STRING("typed-here", out.next.mqtt.password.c_str());
    TEST_ASSERT_TRUE(out.next.mqtt.enabled);
    TEST_ASSERT_EQUAL_STRING(PUSHOVER_USER, out.next.alerts.pushoverUserKey.c_str());
    TEST_ASSERT_TRUE(out.next.alerts.pushoverEnabled);
}

void test_sync_without_secrets_keeps_todays_rule(void)
{
    Config live = following();
    live.mqtt.broker = "10.0.0.5";
    live.mqtt.password = "old-broker-secret";
    const SyncOutcome out = sync(live, sqmeter());      // nothing shared
    TEST_ASSERT_TRUE(out.next.mqtt.password.empty());   // a new broker clears it, as before
    TEST_ASSERT_TRUE(out.next.mqtt.enabled);            // the switch still follows SQMeter's on
    TEST_ASSERT_FALSE(out.next.alerts.pushoverEnabled); // no keys here: Pushover stays off
}

void test_sync_holds_the_mqtt_group_while_roof_commands_are_on(void)
{
    Config live = following();
    live.mqtt.roofCommands = true;
    const SyncOutcome out = sync(live, withSecrets(sqmeter()));
    TEST_ASSERT_FALSE(out.groups[1].changed);
    TEST_ASSERT_FALSE(out.next.mqtt.enabled);
    TEST_ASSERT_TRUE(out.next.mqtt.password.empty());
}

void test_a_save_while_following_keeps_a_typed_password_and_an_off_switch(void)
{
    Config live = following();
    SyncOutcome out = sync(live, withSecrets(sqmeter()));
    applyOutcome(out, live);
    Config next = live;
    next.mqtt.password = "typed";
    next.mqtt.enabled = false;
    keepFollowed(live, next);
    TEST_ASSERT_EQUAL_STRING("typed", next.mqtt.password.c_str());
    TEST_ASSERT_FALSE(next.mqtt.enabled);
}

void test_secrets_are_due_after_each_good_settings_read(void)
{
    LocationSchedule::Pass pass;
    pass.nowMs = 100000;
    pass.enabled = true;
    SecretsRead last;
    TEST_ASSERT_FALSE(secretsDue(pass, 0, last)); // no settings read yet
    TEST_ASSERT_TRUE(secretsDue(pass, 90000, last));
    last = readOf(200, SHARED);
    last.basedOnMs = 90000;
    TEST_ASSERT_FALSE(secretsDue(pass, 90000, last)); // already taken for it
    TEST_ASSERT_TRUE(secretsDue(pass, 95000, last));
    pass.safetyPolled = true;
    TEST_ASSERT_FALSE(secretsDue(pass, 95000, last));
    pass.safetyPolled = false;
    pass.safetyNextMs = pass.nowMs + LocationSchedule::READ_MS - 1; // the safety poll is due within a read
    TEST_ASSERT_FALSE(secretsDue(pass, 95000, last));
    pass.safetyNextMs = 0;
    pass.busy = true;
    TEST_ASSERT_FALSE(secretsDue(pass, 95000, last));
    pass.busy = false;
    pass.enabled = false;
    TEST_ASSERT_FALSE(secretsDue(pass, 95000, last));
}

void test_a_refused_login_is_never_tried_again(void)
{
    // SQMeter locks the endpoint after five failed logins: a wrong saved login
    // must not trigger it. A new login makes the poller forget `last`.
    LocationSchedule::Pass pass;
    pass.nowMs = 100000;
    pass.enabled = true;
    for (bool sent : {true, false})
    {
        SecretsRead last = readOf(401, nullptr, sent);
        last.basedOnMs = 90000;
        TEST_ASSERT_FALSE(secretsDue(pass, 95000, last));
    }
    // A 429 (locked for a minute) waits for the next settings read, minutes later.
    SecretsRead locked = readOf(429, R"({"error":"Too many failed logins; try again in a minute"})");
    TEST_ASSERT_EQUAL_STRING("tooManyLogins", secretsProblemName(locked.problem));
    locked.basedOnMs = 90000;
    TEST_ASSERT_FALSE(secretsDue(pass, 90000, locked));
    TEST_ASSERT_TRUE(secretsDue(pass, 95000, locked));
}

void test_sharing_off_in_the_settings_asks_nothing(void)
{
    DynamicJsonDocument filter(CONFIG_FILTER_BYTES);
    configFilter(filter);
    DynamicJsonDocument doc(CONFIG_DOC_BYTES);
    deserializeJson(doc, R"({"deviceName":"SQMeter","sharing":{"secrets":false}})", DeserializationOption::Filter(filter));
    SiteConfig site;
    std::string why;
    TEST_ASSERT_TRUE(readSiteConfig(doc, site, why));
    TEST_ASSERT_TRUE(sharingOff(site));
    TEST_ASSERT_EQUAL_STRING("sharingOff", secretsProblemName(sharingOffRead(5).problem));
    // On, or an SQMeter without the setting: asked.
    deserializeJson(doc, R"({"deviceName":"SQMeter","sharing":{"secrets":true}})", DeserializationOption::Filter(filter));
    TEST_ASSERT_TRUE(readSiteConfig(doc, site, why));
    TEST_ASSERT_FALSE(sharingOff(site));
    deserializeJson(doc, R"({"deviceName":"SQMeter"})", DeserializationOption::Filter(filter));
    TEST_ASSERT_TRUE(readSiteConfig(doc, site, why));
    TEST_ASSERT_FALSE(sharingOff(site));
}

void test_secrets_pair_only_with_their_settings_read(void)
{
    SecretsRead read = readOf(200, SHARED);
    read.basedOnMs = 5000;
    SiteConfig site = sqmeter();
    pairSecrets(site, 5000, read);
    TEST_ASSERT_TRUE(site.secrets.received);
    pairSecrets(site, 6000, read); // a newer settings read: not paired
    TEST_ASSERT_FALSE(site.secrets.received);
    TEST_ASSERT_TRUE(site.secrets.mqttPassword.empty());
}

void test_the_sync_status_says_why(void)
{
    Config c = following();
    c.safety.httpUrl = "http://192.168.1.50";
    ConfigRead read;
    SyncState state;
    const SecretsRead secrets = readOf(404, nullptr);
    DynamicJsonDocument out(2048);
    writeSyncStatus(out.to<JsonObject>(), SyncView{c, read, state, true, 1000, &secrets});
    TEST_ASSERT_EQUAL_STRING("notSupported", out["secrets"].as<const char *>());
    c.sqmeterSync.mqtt.follow = false;
    c.sqmeterSync.alerts.follow = false;
    out.clear();
    writeSyncStatus(out.to<JsonObject>(), SyncView{c, read, state, true, 1000, &secrets});
    TEST_ASSERT_TRUE(out["secrets"].isNull());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_shared_secrets_are_read);
    RUN_TEST(test_empty_values_mean_not_set);
    RUN_TEST(test_each_refusal_is_told_apart);
    RUN_TEST(test_only_the_saved_address_gets_secrets);
    RUN_TEST(test_the_copy_document_never_carries_a_value);
    RUN_TEST(test_the_copy_document_says_why_not);
    RUN_TEST(test_pushover_needs_both_keys);
    RUN_TEST(test_a_copied_password_is_swapped_in_for_its_broker);
    RUN_TEST(test_a_copied_password_never_goes_to_another_broker);
    RUN_TEST(test_without_a_held_copy_the_placeholder_is_refused);
    RUN_TEST(test_the_placeholder_elsewhere_is_refused);
    RUN_TEST(test_an_empty_held_secret_is_refused);
    RUN_TEST(test_pushover_keys_and_switch_from_the_copy);
    RUN_TEST(test_stored_settings_never_fill);
    RUN_TEST(test_sync_fills_secrets_and_turns_switches_on);
    RUN_TEST(test_sync_never_clears_and_never_turns_off);
    RUN_TEST(test_sync_without_secrets_keeps_todays_rule);
    RUN_TEST(test_sync_holds_the_mqtt_group_while_roof_commands_are_on);
    RUN_TEST(test_a_save_while_following_keeps_a_typed_password_and_an_off_switch);
    RUN_TEST(test_secrets_are_due_after_each_good_settings_read);
    RUN_TEST(test_a_refused_login_is_never_tried_again);
    RUN_TEST(test_sharing_off_in_the_settings_asks_nothing);
    RUN_TEST(test_secrets_pair_only_with_their_settings_read);
    RUN_TEST(test_the_sync_status_says_why);
    return UNITY_END();
}
