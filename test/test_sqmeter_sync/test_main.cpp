#include <unity.h>

#include <ArduinoJson.h>

#include <cstring>
#include <string>

#include "SqmeterSync.h"

using namespace SQM;
using namespace SQM::Site;

// Spec 020: the shared settings keep following SQMeter (FR-025 to FR-031):
// the read of its settings, the sync of each followed group with the normal
// validation, the secrets it clears, the keep rule at a save, the upgrade's
// follow switches and the status section.

void setUp() {}
void tearDown() {}

namespace
{
    SiteConfig sqmeter()
    {
        SiteConfig s;
        s.name = "SQMeter";
        s.hasTime = true;
        s.timezone = "CET-1CEST,M3.5.0,M10.5.0/3";
        s.server1 = "time.cloudflare.com";
        s.server2 = "pool.ntp.org";
        s.hasMqtt = true;
        s.broker = "192.168.1.10";
        s.port = 1884;
        s.username = "obs";
        s.topic = "obs/sqm";
        s.passwordSet = true;
        s.hasNtfy = true;
        s.ntfyServer = "https://ntfy.example";
        s.ntfyTopic = "obs-alerts";
        return s;
    }

    // A device that has its own MQTT and ntfy secrets saved.
    Config live()
    {
        Config c = Config::createDefault();
        c.mqtt.broker = "10.0.0.5";
        c.mqtt.password = "old-broker-secret";
        c.alerts.ntfyServer = "https://ntfy.sh";
        c.alerts.ntfyTopic = "roof";
        c.alerts.ntfyToken = "old-server-token";
        return c;
    }

    constexpr int64_t NOW = 1763540000;

    SyncOutcome sync(const Config &live, const SiteConfig &site, int64_t epoch, bool clockSet)
    {
        SyncOutcome out;
        syncFromSite(out, live, site, epoch, clockSet);
        return out;
    }
} // namespace

// --- Every followed group takes SQMeter's values ----------------------------

void test_each_followed_group_takes_sqmeters_values()
{
    const SyncOutcome out = sync(live(), sqmeter(), NOW, true);
    TEST_ASSERT_TRUE(out.any());
    TEST_ASSERT_EQUAL_STRING("CET-1CEST,M3.5.0,M10.5.0/3", out.next.ntp.timezone.c_str());
    TEST_ASSERT_EQUAL_STRING("time.cloudflare.com", out.next.ntp.server1.c_str());
    TEST_ASSERT_EQUAL_STRING("192.168.1.10", out.next.mqtt.broker.c_str());
    TEST_ASSERT_EQUAL_UINT16(1884, out.next.mqtt.port);
    TEST_ASSERT_EQUAL_STRING("obs", out.next.mqtt.username.c_str());
    TEST_ASSERT_EQUAL_STRING("obs/sqm", out.next.safety.mqttTopic.c_str());
    TEST_ASSERT_EQUAL_STRING("https://ntfy.example", out.next.alerts.ntfyServer.c_str());
    TEST_ASSERT_EQUAL_STRING("obs-alerts", out.next.alerts.ntfyTopic.c_str());
    TEST_ASSERT_EQUAL_UINT32(NOW, out.next.sqmeterSync.time.syncedAt);
    TEST_ASSERT_EQUAL_UINT32(NOW, out.next.sqmeterSync.mqtt.syncedAt);
    TEST_ASSERT_EQUAL_STRING("time, mqtt, alerts", out.changedNames().c_str());
}

// Never RoofRoof's own topic, device name, WiFi, motion or safety settings.
void test_nothing_outside_the_groups_changes()
{
    const Config before = live();
    SyncOutcome out = sync(before, sqmeter(), NOW, true);
    TEST_ASSERT_EQUAL_STRING(before.mqtt.topic.c_str(), out.next.mqtt.topic.c_str());
    // Put back what the groups may change: the rest must be identical.
    out.next.ntp = before.ntp;
    out.next.mqtt = before.mqtt;
    out.next.safety.mqttTopic = before.safety.mqttTopic;
    out.next.alerts.ntfyServer = before.alerts.ntfyServer;
    out.next.alerts.ntfyTopic = before.alerts.ntfyTopic;
    out.next.alerts.ntfyToken = before.alerts.ntfyToken;
    out.next.sqmeterSync = before.sqmeterSync;
    TEST_ASSERT_EQUAL_STRING(before.toJson(false).c_str(), out.next.toJson(false).c_str());
}

void test_a_local_group_is_never_touched()
{
    Config c = live();
    c.sqmeterSync.time.follow = false;
    c.sqmeterSync.mqtt.follow = false;
    c.sqmeterSync.alerts.follow = false;
    const SyncOutcome out = sync(c, sqmeter(), NOW, true);
    TEST_ASSERT_FALSE(out.any());
    TEST_ASSERT_EQUAL_STRING(c.toJson(false).c_str(), out.next.toJson(false).c_str());
}

// --- Secrets never sync; a new server clears the saved one -------------------

void test_a_new_broker_or_ntfy_server_clears_the_saved_secret()
{
    const SyncOutcome out = sync(live(), sqmeter(), NOW, true);
    TEST_ASSERT_EQUAL_STRING("", out.next.mqtt.password.c_str());
    TEST_ASSERT_EQUAL_STRING("", out.next.alerts.ntfyToken.c_str());
}

void test_the_same_broker_and_server_keep_their_secrets()
{
    Config c = live();
    c.mqtt.broker = "192.168.1.10";
    c.mqtt.port = 1884;
    c.mqtt.username = "obs";
    c.alerts.ntfyServer = "https://ntfy.example";
    const SyncOutcome out = sync(c, sqmeter(), NOW, true);
    TEST_ASSERT_EQUAL_STRING("old-broker-secret", out.next.mqtt.password.c_str());
    TEST_ASSERT_EQUAL_STRING("old-server-token", out.next.alerts.ntfyToken.c_str());
}

void test_the_secrets_still_needed_are_named()
{
    Config c = sync(live(), sqmeter(), NOW, true).next;
    TEST_ASSERT_EQUAL(1, secretsNeeded(c, sqmeter(), Group::Mqtt).size());
    TEST_ASSERT_EQUAL_STRING("mqttPassword", secretsNeeded(c, sqmeter(), Group::Mqtt)[0]);
    TEST_ASSERT_EQUAL(0, secretsNeeded(c, sqmeter(), Group::Alerts).size()); // SQMeter has no ntfy token
    SiteConfig withToken = sqmeter();
    withToken.ntfyTokenSet = true;
    TEST_ASSERT_EQUAL_STRING("ntfyToken", secretsNeeded(c, withToken, Group::Alerts)[0]);
    c.mqtt.password = "typed";
    TEST_ASSERT_EQUAL(0, secretsNeeded(c, sqmeter(), Group::Mqtt).size());
    c.sqmeterSync.mqtt.follow = false;
    c.mqtt.password.clear();
    TEST_ASSERT_EQUAL(0, secretsNeeded(c, sqmeter(), Group::Mqtt).size());
}

// Pushover's keys are never synced: a following alerts group asks for them while either is missing here.
void test_pushover_keys_are_asked_for_while_missing()
{
    Config c = sync(live(), sqmeter(), NOW, true).next;
    SiteConfig withPushover = sqmeter();
    withPushover.pushoverUserKeySet = true;
    TEST_ASSERT_EQUAL(1, secretsNeeded(c, withPushover, Group::Alerts).size());
    TEST_ASSERT_EQUAL_STRING("pushoverKeys", secretsNeeded(c, withPushover, Group::Alerts)[0]);
    c.alerts.pushoverUserKey = "u";
    TEST_ASSERT_EQUAL(1, secretsNeeded(c, withPushover, Group::Alerts).size()); // the app token still missing
    c.alerts.pushoverAppToken = "a";
    TEST_ASSERT_EQUAL(0, secretsNeeded(c, withPushover, Group::Alerts).size());
    c.alerts.pushoverUserKey.clear();
    TEST_ASSERT_EQUAL(0, secretsNeeded(c, sqmeter(), Group::Alerts).size()); // SQMeter has no Pushover keys
    c.sqmeterSync.alerts.follow = false;
    TEST_ASSERT_EQUAL(0, secretsNeeded(c, withPushover, Group::Alerts).size());
}

// --- Validation: SQMeter's value is refused, the old one kept ------------------

void test_an_invalid_value_from_sqmeter_is_refused_and_the_old_one_kept()
{
    SiteConfig bad = sqmeter();
    bad.broker = "bad host name!";
    bad.ntfyServer = "not a url";
    bad.server1 = "fd00::10"; // NTP over IPv6 is refused
    Config c = live();
    c.alerts.ntfyEnabled = true; // the ntfy server is checked when ntfy is on
    const SyncOutcome out = sync(c, bad, NOW, true);
    TEST_ASSERT_EQUAL_STRING("10.0.0.5", out.next.mqtt.broker.c_str());
    TEST_ASSERT_EQUAL_STRING("old-broker-secret", out.next.mqtt.password.c_str());
    TEST_ASSERT_EQUAL_STRING("https://ntfy.sh", out.next.alerts.ntfyServer.c_str());
    TEST_ASSERT_EQUAL_STRING("pool.ntp.org", out.next.ntp.server1.c_str());
    TEST_ASSERT_NOT_NULL(std::strstr(out.groups[1].refused.c_str(), "MQTT broker"));
    TEST_ASSERT_NOT_NULL(std::strstr(out.groups[2].refused.c_str(), "ntfy"));
    TEST_ASSERT_NOT_NULL(std::strstr(out.groups[0].refused.c_str(), "NTP server"));
    TEST_ASSERT_FALSE(out.groups[1].changed);
    TEST_ASSERT_EQUAL_UINT32(0, out.next.sqmeterSync.mqtt.syncedAt);
}

// A different broker would change who may move the roof over MQTT.
void test_the_mqtt_group_is_held_while_mqtt_roof_commands_are_on()
{
    Config c = live();
    c.mqtt.roofCommands = true;
    const SyncOutcome out = sync(c, sqmeter(), NOW, true);
    TEST_ASSERT_EQUAL_STRING("10.0.0.5", out.next.mqtt.broker.c_str());
    TEST_ASSERT_EQUAL_STRING("sqmeter", out.next.safety.mqttTopic.c_str());
    TEST_ASSERT_NOT_NULL(std::strstr(out.groups[1].refused.c_str(), "roof commands"));
}

void test_an_older_sqmeter_without_a_part_leaves_its_group_alone()
{
    SiteConfig old;
    old.name = "Old";
    old.hasTime = true;
    old.timezone = "PST8PDT";
    const SyncOutcome out = sync(live(), old, NOW, true);
    TEST_ASSERT_EQUAL_STRING("PST8PDT", out.next.ntp.timezone.c_str());
    TEST_ASSERT_EQUAL_STRING("pool.ntp.org", out.next.ntp.server1.c_str()); // no servers: kept
    TEST_ASSERT_EQUAL_STRING("10.0.0.5", out.next.mqtt.broker.c_str());
    TEST_ASSERT_EQUAL_STRING("roof", out.next.alerts.ntfyTopic.c_str());
}

// --- syncedAt: a write only on a change or a first confirmation ----------------

void test_matching_values_are_marked_once_then_nothing_is_saved()
{
    Config c = sync(live(), sqmeter(), NOW, true).next;
    TEST_ASSERT_FALSE(sync(c, sqmeter(), NOW + 600, true).any());
    Config fresh = c;
    fresh.sqmeterSync.time.syncedAt = 0;
    const SyncOutcome marked = sync(fresh, sqmeter(), NOW + 600, true);
    TEST_ASSERT_TRUE(marked.any());
    TEST_ASSERT_TRUE(marked.marked);
    TEST_ASSERT_EQUAL_UINT32(NOW + 600, marked.next.sqmeterSync.time.syncedAt);
}

void test_without_a_clock_synced_at_is_one()
{
    TEST_ASSERT_EQUAL_UINT32(1, sync(live(), sqmeter(), 5, false).next.sqmeterSync.time.syncedAt);
}

// --- The keep rule at a save (FR-028) ------------------------------------------

void test_a_save_keeps_the_synced_values_of_a_followed_group()
{
    const Config synced = sync(live(), sqmeter(), NOW, true).next;
    Config stale = live(); // a copy taken before the sync, still following
    stale.mqtt.password = "typed-now";
    keepFollowed(synced, stale);
    TEST_ASSERT_EQUAL_STRING("192.168.1.10", stale.mqtt.broker.c_str());
    TEST_ASSERT_EQUAL_STRING("obs/sqm", stale.safety.mqttTopic.c_str());
    TEST_ASSERT_EQUAL_STRING("CET-1CEST,M3.5.0,M10.5.0/3", stale.ntp.timezone.c_str());
    // A secret sent with the old broker goes back with it: never paired with SQMeter's broker.
    TEST_ASSERT_EQUAL_STRING("", stale.mqtt.password.c_str());
    TEST_ASSERT_EQUAL_UINT32(NOW, stale.sqmeterSync.mqtt.syncedAt);
    // A password typed for the synced broker stands.
    Config typed = synced;
    typed.mqtt.password = "typed-now";
    keepFollowed(synced, typed);
    TEST_ASSERT_EQUAL_STRING("typed-now", typed.mqtt.password.c_str());
}

void test_edit_here_instead_takes_the_sent_values()
{
    const Config synced = sync(live(), sqmeter(), NOW, true).next;
    Config edited = synced;
    edited.sqmeterSync.mqtt.follow = false;
    edited.mqtt.broker = "10.9.9.9";
    keepFollowed(synced, edited);
    TEST_ASSERT_EQUAL_STRING("10.9.9.9", edited.mqtt.broker.c_str());
}

// The wizard's copy on a new device: nothing synced yet, the copied values stand.
void test_a_group_never_synced_takes_the_sent_values()
{
    const Config fresh = Config::createDefault();
    Config copied = fresh;
    copied.ntp.timezone = "PST8PDT";
    keepFollowed(fresh, copied);
    TEST_ASSERT_EQUAL_STRING("PST8PDT", copied.ntp.timezone.c_str());
}

// A secret is only asked for the server it belongs to.
void test_no_secret_is_asked_for_a_server_roofroof_doesnt_use()
{
    Config c = live();
    c.mqtt.password.clear();
    c.alerts.ntfyToken.clear();
    SiteConfig withToken = sqmeter();
    withToken.ntfyTokenSet = true;
    TEST_ASSERT_EQUAL(0, secretsNeeded(c, withToken, Group::Mqtt).size()); // RoofRoof's broker isn't SQMeter's
    TEST_ASSERT_EQUAL(0, secretsNeeded(c, withToken, Group::Alerts).size());
    c.mqtt.roofCommands = true; // held: the broker stays RoofRoof's own
    TEST_ASSERT_EQUAL(0, secretsNeeded(sync(c, sqmeter(), NOW, true).next, sqmeter(), Group::Mqtt).size());
}

// A secret sent with a server the keep rule puts back goes back with it.
void test_a_secret_stays_with_its_server_under_the_keep_rule()
{
    const Config synced = sync(live(), sqmeter(), NOW, true).next; // broker 192.168.1.10, password cleared
    Config restored = synced;
    restored.mqtt.broker = "10.0.0.5"; // a backup's broker and its password
    restored.mqtt.password = "backup-secret";
    restored.alerts.ntfyServer = "https://ntfy.sh";
    restored.alerts.ntfyToken = "backup-token";
    keepFollowed(synced, restored);
    TEST_ASSERT_EQUAL_STRING("192.168.1.10", restored.mqtt.broker.c_str());
    TEST_ASSERT_EQUAL_STRING("", restored.mqtt.password.c_str());
    TEST_ASSERT_EQUAL_STRING("https://ntfy.example", restored.alerts.ntfyServer.c_str());
    TEST_ASSERT_EQUAL_STRING("", restored.alerts.ntfyToken.c_str());
}

// While MQTT roof commands are on the device doesn't sync the broker, so it doesn't own it.
void test_the_held_mqtt_group_takes_the_sent_values()
{
    Config synced = sync(live(), sqmeter(), NOW, true).next;
    synced.mqtt.roofCommands = true;
    Config typed = synced;
    typed.mqtt.broker = "10.9.9.9";
    keepFollowed(synced, typed);
    TEST_ASSERT_EQUAL_STRING("10.9.9.9", typed.mqtt.broker.c_str());
}

// The loop copies only what changed into the live settings.
void test_apply_outcome_copies_only_the_changed_groups()
{
    Config c = live();
    c.sqmeterSync.alerts.follow = false;
    const SyncOutcome out = sync(c, sqmeter(), NOW, true);
    Config target = c;
    target.deviceName = "Kept";
    applyOutcome(out, target);
    TEST_ASSERT_EQUAL_STRING("Kept", target.deviceName.c_str());
    TEST_ASSERT_EQUAL_STRING("192.168.1.10", target.mqtt.broker.c_str());
    TEST_ASSERT_EQUAL_STRING("", target.mqtt.password.c_str());
    TEST_ASSERT_EQUAL_STRING("roof", target.alerts.ntfyTopic.c_str());
    TEST_ASSERT_EQUAL_STRING("old-server-token", target.alerts.ntfyToken.c_str());
    TEST_ASSERT_EQUAL_UINT32(NOW, target.sqmeterSync.time.syncedAt);
}

// --- The upgrade's follow switches (FR-029) -------------------------------------

namespace
{
    Config stored(const std::string &json)
    {
        Config cfg = Config::createDefault();
        std::string error;
        TEST_ASSERT_TRUE_MESSAGE(Config::applyJson(json, cfg, false, &error), error.c_str());
        return cfg;
    }
} // namespace

void test_a_new_device_follows_in_every_group()
{
    const Config c = Config::createDefault();
    TEST_ASSERT_TRUE(c.sqmeterSync.time.follow);
    TEST_ASSERT_TRUE(c.sqmeterSync.mqtt.follow);
    TEST_ASSERT_TRUE(c.sqmeterSync.alerts.follow);
}

void test_an_upgrade_keeps_groups_the_person_set_local()
{
    const Config c = stored(
        R"({"ntp":{"timezone":"GMT0BST,M3.5.0/1,M10.5.0"},"mqtt":{"broker":"192.168.1.10"},"alerts":{"ntfy":{"server":"https://ntfy.sh","topic":"roof"}}})");
    TEST_ASSERT_FALSE(c.sqmeterSync.time.follow);
    TEST_ASSERT_FALSE(c.sqmeterSync.mqtt.follow);
    TEST_ASSERT_FALSE(c.sqmeterSync.alerts.follow);
    const Config defaults = stored(R"({"deviceName":"Roof","ntp":{"timezone":"UTC0"}})");
    TEST_ASSERT_TRUE(defaults.sqmeterSync.time.follow);
    TEST_ASSERT_TRUE(defaults.sqmeterSync.mqtt.follow);
    TEST_ASSERT_TRUE(defaults.sqmeterSync.alerts.follow);
    const Config topicOnly = stored(R"({"safety":{"mqtt":{"topic":"observatory/sqm"}}})");
    TEST_ASSERT_FALSE(topicOnly.sqmeterSync.mqtt.follow);
}

void test_the_switches_round_trip_and_the_api_never_sets_synced_at()
{
    Config c = Config::createDefault();
    c.sqmeterSync.mqtt.follow = false;
    c.sqmeterSync.time.syncedAt = 42;
    const Config again = stored(c.toJson(false, false));
    TEST_ASSERT_FALSE(again.sqmeterSync.mqtt.follow);
    TEST_ASSERT_EQUAL_UINT32(42, again.sqmeterSync.time.syncedAt);
    const auto api = Config::fromJson(R"({"sqmeterSync":{"time":{"follow":false,"syncedAt":7}}})", &c);
    TEST_ASSERT_TRUE(api.has_value());
    TEST_ASSERT_FALSE(api->sqmeterSync.time.follow);
    TEST_ASSERT_EQUAL_UINT32(0, api->sqmeterSync.time.syncedAt); // made local: no longer the device's
    const auto keep = Config::fromJson(R"({"sqmeterSync":{"time":{"follow":true,"syncedAt":7}}})", &c);
    TEST_ASSERT_EQUAL_UINT32(42, keep->sqmeterSync.time.syncedAt);
    // From the API there is no upgrade rule: a save that leaves the group's values keeps it
    // following; spec 026: one that changes them without saying makes the group local.
    const auto none = Config::fromJson(R"({"deviceName":"Roof"})", &c);
    TEST_ASSERT_TRUE(none->sqmeterSync.time.follow);
    const auto typed = Config::fromJson(R"({"ntp":{"timezone":"PST8PDT"}})", &c);
    TEST_ASSERT_FALSE(typed->sqmeterSync.time.follow);
}

// --- The read of SQMeter's settings and the status ---------------------------------

void test_the_settings_read_and_a_failed_one_keeps_the_last_good()
{
    DynamicJsonDocument filter(CONFIG_FILTER_BYTES);
    configFilter(filter);
    DynamicJsonDocument doc(CONFIG_DOC_BYTES);
    Fetch fetch;
    fetch.status = 200;
    fetch.parsed = !deserializeJson(
        doc, R"({"deviceName":"S","ntp":{"timezone":"UTC0","server1":"a","server2":""}})", DeserializationOption::Filter(filter));
    ConfigRead kept;
    record(kept, readConfig(fetch, doc, 1000));
    TEST_ASSERT_TRUE(kept.good());
    Fetch none;
    none.error = "connection refused";
    record(kept, readConfig(none, doc, 9000));
    TEST_ASSERT_EQUAL(Problem::Unreachable, kept.problem);
    TEST_ASSERT_EQUAL_UINT64(1000, kept.goodAtMs);
    TEST_ASSERT_EQUAL_STRING("a", kept.site.server1.c_str());
    Fetch notFound;
    notFound.status = 404;
    TEST_ASSERT_EQUAL(Problem::Unreadable, readConfig(notFound, doc, 1).problem);
}

void test_the_status_section()
{
    Config c = sync(live(), sqmeter(), NOW, true).next;
    c.sqmeterSync.alerts.follow = false;
    ConfigRead read;
    read.problem = Problem::None;
    read.site = sqmeter();
    read.goodAtMs = 10000;
    SyncState state;
    state.refused[0] = "SQMeter's value refused: NTP server";
    DynamicJsonDocument doc(1024);
    writeSyncStatus(doc.to<JsonObject>(), SyncView{c, read, state, true, 70000});
    std::string json;
    serializeJson(doc, json);
    TEST_ASSERT_EQUAL_STRING(
        R"({"problem":null,"readAgeSeconds":60,"secrets":null,)"
        R"("time":{"follow":true,"syncedAt":1763540000,"refused":"SQMeter's value refused: NTP server","needs":[]},)"
        R"("mqtt":{"follow":true,"syncedAt":1763540000,"refused":null,"needs":["mqttPassword"]},)"
        R"("alerts":{"follow":false,"syncedAt":1763540000,"refused":null,"needs":[]}})",
        json.c_str());
}

void test_the_status_without_an_address_or_with_nothing_followed()
{
    Config c = Config::createDefault();
    DynamicJsonDocument doc(1024);
    writeSyncStatus(doc.to<JsonObject>(), SyncView{c, ConfigRead{}, SyncState{}, false, 1000});
    TEST_ASSERT_EQUAL_STRING("noAddress", doc["problem"]);
    c.sqmeterSync.time.follow = c.sqmeterSync.mqtt.follow = c.sqmeterSync.alerts.follow = false;
    writeSyncStatus(doc.to<JsonObject>(), SyncView{c, ConfigRead{}, SyncState{}, false, 1000});
    TEST_ASSERT_TRUE(doc["problem"].isNull());
}

// The bigger read: a settings document with long alert templates still fits
// the filtered document (the templates are skipped as they arrive).
void test_a_large_settings_document_fits_the_filtered_document()
{
    std::string body = R"({"deviceName":"SQMeter","alerts":{"events":{)";
    for (int i = 0; i < 20; ++i)
        body += (i ? "," : "") + std::string("\"e") + std::to_string(i) + R"(":{"level":3,"title":")" + std::string(80, 't') +
                R"(","message":")" + std::string(400, 'm') + R"(","sound":""}})";
    body +=
        R"(},"ntfy":{"server":"https://ntfy.sh","topic":"t","token":"********"}},"ntp":{"timezone":"UTC0","server1":"a","server2":"b"},)"
        R"("mqtt":{"broker":"b","port":1883,"username":"u","password":"********","topic":"t"},"location":{"set":true,"latitude":1,"longitude":2}})";
    TEST_ASSERT_GREATER_THAN(10000, body.size());
    DynamicJsonDocument filter(CONFIG_FILTER_BYTES);
    configFilter(filter);
    DynamicJsonDocument doc(CONFIG_DOC_BYTES);
    TEST_ASSERT_TRUE(deserializeJson(doc, body, DeserializationOption::Filter(filter)) == DeserializationError::Ok);
    TEST_ASSERT_FALSE(doc.overflowed());
    printf("filtered settings: %u of %u bytes (host)\n", static_cast<unsigned>(doc.memoryUsage()), static_cast<unsigned>(CONFIG_DOC_BYTES));
}

// --- Spec 026: typing in a followed group sets it here --------------------------------

namespace
{
    // A save from the UI/API (strict) onto `cfg`.
    void saved(Config &cfg, const std::string &json)
    {
        std::string error;
        TEST_ASSERT_TRUE_MESSAGE(Config::applyJson(json, cfg, true, &error), error.c_str());
    }
} // namespace

void test_a_save_that_changes_a_followed_group_without_saying_unfollows_it()
{
    Config c = sync(live(), sqmeter(), NOW, true).next;
    saved(c, R"({"mqtt":{"username":"me","password":"mine"}})");
    TEST_ASSERT_FALSE(c.sqmeterSync.mqtt.follow);
    TEST_ASSERT_EQUAL_UINT32(0, c.sqmeterSync.mqtt.syncedAt);
    TEST_ASSERT_TRUE(c.sqmeterSync.time.follow); // the other groups keep following
    TEST_ASSERT_TRUE(c.sqmeterSync.alerts.follow);
    // The keep rule then leaves the typed values alone: they are never overwritten.
    const Config before = sync(live(), sqmeter(), NOW, true).next;
    keepFollowed(before, c);
    TEST_ASSERT_EQUAL_STRING("me", c.mqtt.username.c_str());
    TEST_ASSERT_EQUAL_STRING("mine", c.mqtt.password.c_str());
    // And the next sync doesn't touch a local group.
    TEST_ASSERT_EQUAL_STRING("me", sync(c, sqmeter(), NOW + 600, true).next.mqtt.username.c_str());
}

void test_a_save_that_says_follow_keeps_following()
{
    Config c = sync(live(), sqmeter(), NOW, true).next;
    saved(c, R"({"ntp":{"timezone":"PST8PDT"},"sqmeterSync":{"time":{"follow":true}}})");
    TEST_ASSERT_TRUE(c.sqmeterSync.time.follow); // a copy from SQMeter says so
}

void test_a_save_that_leaves_the_groups_alone_keeps_following()
{
    Config c = sync(live(), sqmeter(), NOW, true).next;
    saved(c, R"({"deviceName":"Roof","mqtt":{"password":"********"},"alerts":{"ntfy":{"server":"https://ntfy.example"}}})");
    TEST_ASSERT_TRUE(c.sqmeterSync.mqtt.follow);   // the mask keeps the password: nothing changed
    TEST_ASSERT_TRUE(c.sqmeterSync.alerts.follow); // the same server
    TEST_ASSERT_TRUE(c.sqmeterSync.time.follow);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_each_followed_group_takes_sqmeters_values);
    RUN_TEST(test_nothing_outside_the_groups_changes);
    RUN_TEST(test_a_local_group_is_never_touched);
    RUN_TEST(test_a_new_broker_or_ntfy_server_clears_the_saved_secret);
    RUN_TEST(test_the_same_broker_and_server_keep_their_secrets);
    RUN_TEST(test_the_secrets_still_needed_are_named);
    RUN_TEST(test_pushover_keys_are_asked_for_while_missing);
    RUN_TEST(test_an_invalid_value_from_sqmeter_is_refused_and_the_old_one_kept);
    RUN_TEST(test_the_mqtt_group_is_held_while_mqtt_roof_commands_are_on);
    RUN_TEST(test_an_older_sqmeter_without_a_part_leaves_its_group_alone);
    RUN_TEST(test_matching_values_are_marked_once_then_nothing_is_saved);
    RUN_TEST(test_without_a_clock_synced_at_is_one);
    RUN_TEST(test_a_save_keeps_the_synced_values_of_a_followed_group);
    RUN_TEST(test_edit_here_instead_takes_the_sent_values);
    RUN_TEST(test_a_group_never_synced_takes_the_sent_values);
    RUN_TEST(test_no_secret_is_asked_for_a_server_roofroof_doesnt_use);
    RUN_TEST(test_a_secret_stays_with_its_server_under_the_keep_rule);
    RUN_TEST(test_the_held_mqtt_group_takes_the_sent_values);
    RUN_TEST(test_apply_outcome_copies_only_the_changed_groups);
    RUN_TEST(test_a_new_device_follows_in_every_group);
    RUN_TEST(test_an_upgrade_keeps_groups_the_person_set_local);
    RUN_TEST(test_the_switches_round_trip_and_the_api_never_sets_synced_at);
    RUN_TEST(test_the_settings_read_and_a_failed_one_keeps_the_last_good);
    RUN_TEST(test_the_status_section);
    RUN_TEST(test_the_status_without_an_address_or_with_nothing_followed);
    RUN_TEST(test_a_large_settings_document_fits_the_filtered_document);
    RUN_TEST(test_a_save_that_changes_a_followed_group_without_saying_unfollows_it);
    RUN_TEST(test_a_save_that_says_follow_keeps_following);
    RUN_TEST(test_a_save_that_leaves_the_groups_alone_keeps_following);
    return UNITY_END();
}
