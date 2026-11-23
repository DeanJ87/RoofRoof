#include <unity.h>

#include <ArduinoJson.h>

#include <cmath>
#include <cstring>
#include <string>

#include "SqmeterSite.h"

using namespace SQM;
using namespace SQM::Site;

// Spec 020: site data from SQMeter. Its address (FR-002), the location read
// (FR-003), the schedule (FR-004, FR-005), the save rule (FR-007), the
// status section (FR-010) and the copy's site document (FR-014, FR-018).

void setUp() {}
void tearDown() {}

namespace
{
    bool near(double expected, double actual)
    {
        return std::fabs(expected - actual) < 1e-9;
    }

    // A fetch of SQMeter's status, filtered as the poller filters it.
    LocationRead readStatus(const std::string &body, int status = 200, uint64_t atMs = 1000)
    {
        DynamicJsonDocument filter(STATUS_FILTER_BYTES);
        statusFilter(filter);
        DynamicJsonDocument doc(STATUS_DOC_BYTES);
        Fetch fetch;
        fetch.status = status;
        fetch.parsed = deserializeJson(doc, body, DeserializationOption::Filter(filter)) == DeserializationError::Ok;
        return readLocation(fetch, doc, atMs);
    }

    // SQMeter's settings, filtered as the copy filters them, as the site document.
    std::string siteDocument(const std::string &body, bool *ok = nullptr, std::string *why = nullptr)
    {
        DynamicJsonDocument filter(CONFIG_FILTER_BYTES);
        configFilter(filter);
        DynamicJsonDocument doc(CONFIG_DOC_BYTES);
        TEST_ASSERT_TRUE(deserializeJson(doc, body, DeserializationOption::Filter(filter)) == DeserializationError::Ok);
        SiteConfig site;
        std::string reason;
        const bool read = readSiteConfig(doc, site, reason);
        if (ok != nullptr)
            *ok = read;
        if (why != nullptr)
            *why = reason;
        DynamicJsonDocument out(1024);
        writeSiteDocument(out.to<JsonObject>(), "http://192.168.1.50/api/safety", site);
        std::string json;
        serializeJson(out, json);
        return json;
    }

    LocationRead good(double lat, double lon, bool gps)
    {
        LocationRead read;
        read.problem = Problem::None;
        read.latitude = lat;
        read.longitude = lon;
        read.gps = gps;
        read.atMs = read.goodAtMs = 5000;
        return read;
    }

    LocationConfig kept(double lat, double lon, bool gps)
    {
        LocationConfig loc;
        loc.source = LocationConfig::Source::Sqmeter;
        loc.set = true;
        loc.latitude = lat;
        loc.longitude = lon;
        loc.gps = gps;
        loc.savedAt = 1;
        return loc;
    }

    std::string statusJson(const LocationConfig &config, const LocationRead &read, bool addressSet, uint64_t nowMs)
    {
        DynamicJsonDocument doc(512);
        writeLocationStatus(doc.to<JsonObject>(), config, read, addressSet, nowMs);
        std::string json;
        serializeJson(doc, json);
        return json;
    }

    // SQMeter v0.4's settings, as GET /api/config shows them (secrets masked), cut down.
    const char *const CURRENT_CONFIG =
        R"({"deviceName":"SQMeter","language":"de","wifi":{"ssid":"Home","password":"********","hostname":"sqmeter"},)"
        R"("mqtt":{"enabled":true,"broker":"192.168.1.10","port":1884,"username":"obs","password":"********","topic":"obs/sqm",)"
        R"("publish":{"sky":true}},"ota":{"password":"********"},"auth":{"password":"********"},)"
        R"("ntp":{"enabled":true,"server1":"time.cloudflare.com","server2":"pool.ntp.org","syncIntervalMs":600000,"timezone":"CET-1CEST,M3.5.0,M10.5.0/3"},)"
        R"("location":{"set":true,"latitude":48.1374,"longitude":11.5755,"showSunMoon":true},)"
        R"("alerts":{"enabled":true,"nightSunAltitudeDeg":-12,"events":{"rain_started":{"level":3,"message":"It rains at {device}","sound":"","title":"Rain"}},)"
        R"("ntfy":{"enabled":true,"server":"https://ntfy.example","token":"","topic":"obs-alerts"},)"
        R"("pushover":{"enabled":true,"userKey":"********","appToken":"********","sound":""},)"
        R"("webhook":{"enabled":false,"url":"","authHeader":"********","insecureTls":false}}})";
} // namespace

// --- The address -------------------------------------------------------------

void test_the_site_url_keeps_the_host_and_port_and_takes_sqmeters_path()
{
    TEST_ASSERT_EQUAL_STRING("http://192.168.1.50/api/status", siteUrl("http://192.168.1.50/api/safety", STATUS_PATH).c_str());
    TEST_ASSERT_EQUAL_STRING("http://sqm.local:8080/api/config", siteUrl("http://sqm.local:8080/proxy/api/safety", CONFIG_PATH).c_str());
    TEST_ASSERT_EQUAL_STRING("http://[fd00::10]/api/status", siteUrl("http://[fd00::10]/api/safety", STATUS_PATH).c_str());
    TEST_ASSERT_EQUAL_STRING("http://192.168.1.50/api/status", siteUrl("http://192.168.1.50", STATUS_PATH).c_str());
}

void test_no_site_url_without_a_valid_http_address()
{
    TEST_ASSERT_EQUAL_STRING("", siteUrl("", STATUS_PATH).c_str());
    TEST_ASSERT_EQUAL_STRING("", siteUrl("https://192.168.1.50/api/safety", STATUS_PATH).c_str());
    TEST_ASSERT_EQUAL_STRING("", siteUrl("sqmeter.local", STATUS_PATH).c_str());
    TEST_ASSERT_EQUAL_STRING("", siteUrl("http://", STATUS_PATH).c_str());
}

// --- The location read (FR-003) --------------------------------------------------

void test_a_gps_location_is_read()
{
    const LocationRead read = readStatus(
        R"({"uptime":5,"sky":{"locationSource":"gps","nightKnown":true,"latitude":51.5074,"longitude":-0.1278,"isNight":false,"sunAltitudeDeg":30.7},"freeHeap":1})");
    TEST_ASSERT_TRUE(read.good());
    TEST_ASSERT_TRUE(read.gps);
    TEST_ASSERT_TRUE(near(51.5074, read.latitude));
    TEST_ASSERT_TRUE(near(-0.1278, read.longitude));
    TEST_ASSERT_EQUAL_UINT64(1000, read.goodAtMs);
}

void test_a_typed_sqmeter_location_is_read_as_not_gps()
{
    const LocationRead read = readStatus(R"({"sky":{"locationSource":"manual","nightKnown":false,"latitude":-33.86,"longitude":151.2}})");
    TEST_ASSERT_TRUE(read.good());
    TEST_ASSERT_FALSE(read.gps);
}

void test_no_location_at_sqmeter()
{
    TEST_ASSERT_EQUAL(Problem::NoLocation, readStatus(R"({"sky":{"locationSource":"none","nightKnown":false}})").problem);
    // Before SQMeter v0.2 the status had no sky section.
    TEST_ASSERT_EQUAL(Problem::NoLocation, readStatus(R"({"uptime":5,"freeHeap":1})").problem);
}

void test_a_location_out_of_range_or_not_a_number_is_unreadable()
{
    TEST_ASSERT_EQUAL(Problem::Unreadable, readStatus(R"({"sky":{"locationSource":"gps","latitude":91,"longitude":0}})").problem);
    TEST_ASSERT_EQUAL(Problem::Unreadable, readStatus(R"({"sky":{"locationSource":"gps","latitude":0,"longitude":-180.5}})").problem);
    TEST_ASSERT_EQUAL(Problem::Unreadable, readStatus(R"({"sky":{"locationSource":"gps","latitude":"51","longitude":0}})").problem);
    TEST_ASSERT_EQUAL(Problem::Unreadable, readStatus(R"({"sky":{"locationSource":"gps","latitude":true,"longitude":0}})").problem);
    TEST_ASSERT_EQUAL(Problem::Unreadable, readStatus(R"({"sky":{"locationSource":"gps","longitude":0}})").problem);
}

void test_a_bad_reply_is_unreadable_and_no_reply_unreachable()
{
    const LocationRead notFound = readStatus("<html>", 404);
    TEST_ASSERT_EQUAL(Problem::Unreadable, notFound.problem);
    TEST_ASSERT_EQUAL_STRING("HTTP 404", notFound.detail.c_str());
    TEST_ASSERT_EQUAL(Problem::Unreadable, readStatus("not json").problem);
    TEST_ASSERT_EQUAL(Problem::Unreadable, readStatus("[1,2]").problem);
    Fetch none;
    none.error = "connection refused";
    DynamicJsonDocument doc(64);
    const LocationRead lost = readLocation(none, doc, 9);
    TEST_ASSERT_EQUAL(Problem::Unreachable, lost.problem);
    TEST_ASSERT_EQUAL_STRING("connection refused", lost.detail.c_str());
}

void test_a_failed_read_keeps_the_time_of_the_last_good_one()
{
    LocationRead current;
    record(current, readStatus(R"({"sky":{"locationSource":"gps","latitude":1,"longitude":2}})", 200, 1000));
    record(current, readStatus("", 500, 7000));
    TEST_ASSERT_EQUAL(Problem::Unreadable, current.problem);
    TEST_ASSERT_EQUAL_UINT64(1000, current.goodAtMs);
    TEST_ASSERT_EQUAL_UINT64(7000, current.atMs);
}

// --- The schedule (FR-004, FR-005) ---------------------------------------------------

namespace
{
    LocationSchedule::Pass pass(uint64_t nowMs, uint32_t generation = 1)
    {
        LocationSchedule::Pass p;
        p.nowMs = nowMs;
        p.enabled = true;
        p.generation = generation;
        return p;
    }

    // A schedule whose poller started at 0: its first read is due at FIRST_MS.
    LocationSchedule startedAtZero()
    {
        LocationSchedule schedule;
        TEST_ASSERT_FALSE(schedule.due(pass(0)));
        return schedule;
    }
} // namespace

void test_the_first_read_is_15_s_after_the_start()
{
    LocationSchedule schedule;
    TEST_ASSERT_FALSE(schedule.due(pass(1000)));
    TEST_ASSERT_FALSE(schedule.due(pass(15999)));
    TEST_ASSERT_TRUE(schedule.due(pass(16000)));
}

void test_reads_repeat_after_10_min_when_good_and_2_min_when_failed()
{
    LocationSchedule schedule = startedAtZero();
    TEST_ASSERT_TRUE(schedule.due(pass(20000)));
    schedule.done(true, 20000);
    TEST_ASSERT_FALSE(schedule.due(pass(20000 + 599999)));
    TEST_ASSERT_TRUE(schedule.due(pass(20000 + 600000)));
    schedule.done(false, 700000);
    TEST_ASSERT_FALSE(schedule.due(pass(700000 + 119999)));
    TEST_ASSERT_TRUE(schedule.due(pass(700000 + 120000)));
}

void test_a_change_of_address_or_source_makes_a_read_due_at_once()
{
    LocationSchedule schedule = startedAtZero();
    TEST_ASSERT_TRUE(schedule.due(pass(20000)));
    schedule.done(true, 20000);
    TEST_ASSERT_FALSE(schedule.due(pass(30000)));
    TEST_ASSERT_TRUE(schedule.due(pass(30000, 2)));
}

void test_never_while_busy_or_in_a_safety_poll_pass()
{
    LocationSchedule schedule = startedAtZero();
    LocationSchedule::Pass p = pass(20000);
    p.busy = true;
    TEST_ASSERT_FALSE(schedule.due(p));
    p.busy = false;
    p.safetyPolled = true;
    TEST_ASSERT_FALSE(schedule.due(p));
    p.safetyPolled = false;
    TEST_ASSERT_TRUE(schedule.due(p));
}

// A read never delays SQMeter's safety poll: none starts within READ_MS of it.
void test_never_when_the_safety_poll_is_due_within_a_read()
{
    LocationSchedule schedule = startedAtZero();
    LocationSchedule::Pass p = pass(20000);
    p.safetyNextMs = 20000 + LocationSchedule::READ_MS - 1;
    TEST_ASSERT_FALSE(schedule.due(p));
    p.safetyNextMs = 20000 + LocationSchedule::READ_MS;
    TEST_ASSERT_TRUE(schedule.due(p));
    p.safetyNextMs = 0; // the safety source is off
    TEST_ASSERT_TRUE(schedule.due(p));
}

void test_never_when_not_following_sqmeter()
{
    LocationSchedule schedule;
    LocationSchedule::Pass p = pass(10000000);
    p.enabled = false;
    TEST_ASSERT_FALSE(schedule.due(p));
}

// --- The save rule (FR-007) -------------------------------------------------------

void test_a_manual_location_is_never_saved_over()
{
    LocationConfig manual = kept(1, 2, false);
    manual.source = LocationConfig::Source::Manual;
    TEST_ASSERT_FALSE(locationToSave(manual, good(10, 20, true), 1763540000, true).has_value());
}

void test_a_failed_read_is_never_saved()
{
    LocationRead bad = good(10, 20, true);
    bad.problem = Problem::NoLocation;
    TEST_ASSERT_FALSE(locationToSave(kept(1, 2, true), bad, 1763540000, true).has_value());
}

void test_the_first_good_read_is_saved_with_its_time()
{
    LocationConfig none;
    const auto next = locationToSave(none, good(51.5074, -0.1278, true), 1763540000, true);
    TEST_ASSERT_TRUE(next.has_value());
    TEST_ASSERT_TRUE(next->set);
    TEST_ASSERT_TRUE(near(51.5074, next->latitude));
    TEST_ASSERT_TRUE(near(-0.1278, next->longitude));
    TEST_ASSERT_TRUE(next->gps);
    TEST_ASSERT_EQUAL_UINT32(1763540000, next->savedAt);
    TEST_ASSERT_EQUAL(LocationConfig::Source::Sqmeter, next->source);
}

void test_a_jitter_under_a_thousandth_of_a_degree_is_not_saved()
{
    const LocationConfig current = kept(51.5074, -0.1278, true);
    TEST_ASSERT_FALSE(locationToSave(current, good(51.5083, -0.1269, true), 1763540000, true).has_value());
    TEST_ASSERT_FALSE(locationToSave(current, good(51.5065, -0.1287, true), 1763540000, true).has_value());
}

void test_a_move_of_a_thousandth_of_a_degree_is_saved()
{
    const LocationConfig current = kept(51.5, -0.1, true);
    TEST_ASSERT_TRUE(locationToSave(current, good(51.5011, -0.1, true), 1763540000, true).has_value());
    TEST_ASSERT_TRUE(locationToSave(current, good(51.5, -0.1011, true), 1763540000, true).has_value());
}

void test_a_change_of_the_gps_flag_is_saved()
{
    TEST_ASSERT_TRUE(locationToSave(kept(51.5, -0.1, true), good(51.5, -0.1, false), 1763540000, true).has_value());
}

void test_without_a_clock_the_time_is_zero()
{
    const auto next = locationToSave(LocationConfig{}, good(1, 2, false), 12, false);
    TEST_ASSERT_TRUE(next.has_value());
    TEST_ASSERT_EQUAL_UINT32(0, next->savedAt);
}

// FR-008 at the save: an older copy of the settings can't put an older location back.
void test_a_save_keeps_the_live_location_while_both_follow_sqmeter()
{
    LocationConfig next = kept(10, 20, false);
    keepDeviceLocation(kept(51.5, -0.1, true), next);
    TEST_ASSERT_TRUE(near(51.5, next.latitude));
    TEST_ASSERT_TRUE(next.gps);
    LocationConfig manual = kept(10, 20, false);
    manual.source = LocationConfig::Source::Manual;
    keepDeviceLocation(kept(51.5, -0.1, true), manual);
    TEST_ASSERT_TRUE(near(10, manual.latitude));
    LocationConfig restored = kept(10, 20, false);
    keepDeviceLocation(LocationConfig{}, restored); // nothing kept: the sent one stands
    TEST_ASSERT_TRUE(near(10, restored.latitude));
}

// --- The status section (FR-010) ---------------------------------------------------

void test_the_status_names_the_source_age_and_problem()
{
    LocationRead read = good(51.5, -0.1, true);
    read.goodAtMs = 10000;
    TEST_ASSERT_EQUAL_STRING(
        R"({"source":"sqmeter","set":true,"gps":true,"savedAt":1,"readAgeSeconds":180,"problem":null})",
        statusJson(kept(51.5, -0.1, true), read, true, 190000).c_str());
    read.problem = Problem::Unreachable;
    TEST_ASSERT_EQUAL_STRING(
        R"({"source":"sqmeter","set":true,"gps":true,"savedAt":1,"readAgeSeconds":180,"problem":"unreachable"})",
        statusJson(kept(51.5, -0.1, true), read, true, 190000).c_str());
}

void test_the_status_before_any_read_and_without_an_address()
{
    const LocationRead waiting;
    TEST_ASSERT_EQUAL_STRING(
        R"({"source":"sqmeter","set":false,"gps":false,"savedAt":null,"readAgeSeconds":null,"problem":"waiting"})",
        statusJson(LocationConfig{}, waiting, true, 1000).c_str());
    TEST_ASSERT_EQUAL_STRING(
        R"({"source":"sqmeter","set":false,"gps":false,"savedAt":null,"readAgeSeconds":null,"problem":"noAddress"})",
        statusJson(LocationConfig{}, waiting, false, 1000).c_str());
}

void test_a_manual_location_has_no_sqmeter_status()
{
    LocationConfig manual = kept(1, 2, true);
    manual.source = LocationConfig::Source::Manual;
    TEST_ASSERT_EQUAL_STRING(
        R"({"source":"manual","set":true,"gps":false,"savedAt":null,"readAgeSeconds":null,"problem":null})",
        statusJson(manual, good(1, 2, true), false, 99000).c_str());
}

// --- The copy's site document (FR-014, FR-018) ------------------------------------------

void test_the_site_document_of_a_current_sqmeter()
{
    bool ok = false;
    const std::string json = siteDocument(CURRENT_CONFIG, &ok);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING(
        R"({"address":"http://192.168.1.50","device":{"name":"SQMeter"},)"
        R"("location":{"set":true,"latitude":48.1374,"longitude":11.5755},)"
        R"("time":{"timezone":"CET-1CEST,M3.5.0,M10.5.0/3","server1":"time.cloudflare.com","server2":"pool.ntp.org"},)"
        R"("mqtt":{"broker":"192.168.1.10","port":1884,"username":"obs","topic":"obs/sqm","passwordSet":true},)"
        R"("ntfy":{"server":"https://ntfy.example","topic":"obs-alerts","tokenSet":false},)"
        R"("pushover":{"userKeySet":true,"appTokenSet":true}})",
        json.c_str());
}

// SC-005: an SQMeter old enough not to mask its secrets still never leaks one.
void test_an_unmasked_secret_is_only_ever_reported_as_set()
{
    const std::string json = siteDocument(
        R"({"deviceName":"Old","mqtt":{"broker":"b","port":1883,"username":"u","password":"hunter2-mqtt","topic":"t"},)"
        R"("wifi":{"password":"hunter2-wifi"},"auth":{"password":"hunter2-auth"},)"
        R"("alerts":{"ntfy":{"server":"s","topic":"t","token":"hunter2-ntfy"},"pushover":{"userKey":"hunter2-user","appToken":"hunter2-app"},)"
        R"("webhook":{"authHeader":"hunter2-hook"}}})");
    TEST_ASSERT_NULL(std::strstr(json.c_str(), "hunter2"));
    TEST_ASSERT_NOT_NULL(std::strstr(json.c_str(), "\"passwordSet\":true"));
    TEST_ASSERT_NOT_NULL(std::strstr(json.c_str(), "\"tokenSet\":true"));
    TEST_ASSERT_NOT_NULL(std::strstr(json.c_str(), "\"userKeySet\":true"));
}

void test_a_v0_1_sqmeter_has_its_time_zone_at_the_top_and_nothing_else()
{
    const std::string json = siteDocument(
        R"({"deviceName":"SQM","timezone":"PST8PDT","mqtt":{"broker":"","port":1883,"username":"","password":"","topic":"sqm"}})");
    TEST_ASSERT_EQUAL_STRING(
        R"({"address":"http://192.168.1.50","device":{"name":"SQM"},"location":null,"time":{"timezone":"PST8PDT","server1":"","server2":""},)"
        R"("mqtt":{"broker":"","port":1883,"username":"","topic":"sqm","passwordSet":false},"ntfy":null,"pushover":null})",
        json.c_str());
}

void test_a_location_not_set_at_sqmeter_has_no_coordinates()
{
    const std::string json = siteDocument(R"({"deviceName":"S","location":{"set":false,"latitude":12,"longitude":34}})");
    TEST_ASSERT_NOT_NULL(std::strstr(json.c_str(), R"("location":{"set":false,"latitude":0,"longitude":0})"));
}

void test_a_document_that_isnt_sqmeters_settings_is_refused()
{
    bool ok = true;
    std::string why;
    siteDocument(R"({"safe":true})", &ok, &why);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_STRING("not SQMeter's settings", why.c_str());
    siteDocument("[]", &ok);
    TEST_ASSERT_FALSE(ok);
}

void test_a_bad_mqtt_port_reads_as_the_default()
{
    const std::string json = siteDocument(R"({"deviceName":"S","mqtt":{"broker":"b","port":70000}})");
    TEST_ASSERT_NOT_NULL(std::strstr(json.c_str(), "\"port\":1883"));
}

// The filter keeps the parsed settings small whatever the alert templates hold.
void test_the_filter_drops_everything_else()
{
    DynamicJsonDocument filter(CONFIG_FILTER_BYTES);
    configFilter(filter);
    DynamicJsonDocument doc(CONFIG_DOC_BYTES);
    TEST_ASSERT_TRUE(deserializeJson(doc, CURRENT_CONFIG, DeserializationOption::Filter(filter)) == DeserializationError::Ok);
    TEST_ASSERT_TRUE(doc["wifi"].isNull());
    TEST_ASSERT_TRUE(doc["language"].isNull());
    TEST_ASSERT_TRUE(doc["alerts"]["events"].isNull());
    TEST_ASSERT_TRUE(doc["alerts"]["webhook"].isNull());
    TEST_ASSERT_FALSE(filter.overflowed());
    TEST_ASSERT_FALSE(doc.overflowed());
}

// --- SQMeter's login (spec 020) ----------------------------------------------

void test_a_401_or_403_says_whether_a_password_is_wanted_or_refused()
{
    Fetch noLogin;
    noLogin.status = 401;
    DynamicJsonDocument doc(64);
    LocationRead read = readLocation(noLogin, doc, 1);
    TEST_ASSERT_EQUAL(Problem::PasswordNeeded, read.problem);
    TEST_ASSERT_EQUAL_STRING("SQMeter wants a password", read.detail.c_str());
    TEST_ASSERT_EQUAL_STRING("passwordNeeded", problemName(read.problem));
    Fetch refused;
    refused.status = 403;
    refused.credentials = true;
    read = readLocation(refused, doc, 1);
    TEST_ASSERT_EQUAL(Problem::PasswordRefused, read.problem);
    TEST_ASSERT_EQUAL_STRING("SQMeter refused the password", read.detail.c_str());
    TEST_ASSERT_EQUAL_STRING("passwordRefused", problemName(read.problem));
    TEST_ASSERT_EQUAL(Problem::Unreadable, problemOfStatus(500, true));
}

void test_the_copy_sends_the_saved_login_only_to_its_own_host()
{
    SafetyConfig saved;
    saved.httpUrl = "http://192.168.1.50/api/safety";
    saved.httpUsername = "admin";
    saved.httpPassword = "saved-secret";
    Endpoint same = copyEndpoint(saved, "http://192.168.1.50", TypedLogin{});
    TEST_ASSERT_EQUAL_STRING("http://192.168.1.50/api/config", same.url.c_str());
    TEST_ASSERT_EQUAL_STRING("admin", same.username.c_str());
    TEST_ASSERT_EQUAL_STRING("saved-secret", same.password.c_str());
    TypedLogin masked;
    masked.password = "********";
    TEST_ASSERT_EQUAL_STRING("saved-secret", copyEndpoint(saved, "http://192.168.1.50/x", masked).password.c_str());
    // Another host: never the saved password (nor user name).
    const Endpoint other = copyEndpoint(saved, "http://192.168.1.51/api/safety", masked);
    TEST_ASSERT_EQUAL_STRING("", other.password.c_str());
    TEST_ASSERT_EQUAL_STRING("", other.username.c_str());
    TEST_ASSERT_FALSE(other.hasLogin());
    TypedLogin typed;
    typed.username = "obs";
    typed.password = "typed";
    const Endpoint typedOther = copyEndpoint(saved, "http://192.168.1.51", typed);
    TEST_ASSERT_EQUAL_STRING("obs", typedOther.username.c_str());
    TEST_ASSERT_EQUAL_STRING("typed", typedOther.password.c_str());
    // Another port is another host.
    TEST_ASSERT_EQUAL_STRING("", copyEndpoint(saved, "http://192.168.1.50:8080", TypedLogin{}).password.c_str());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_site_url_keeps_the_host_and_port_and_takes_sqmeters_path);
    RUN_TEST(test_no_site_url_without_a_valid_http_address);
    RUN_TEST(test_a_gps_location_is_read);
    RUN_TEST(test_a_typed_sqmeter_location_is_read_as_not_gps);
    RUN_TEST(test_no_location_at_sqmeter);
    RUN_TEST(test_a_location_out_of_range_or_not_a_number_is_unreadable);
    RUN_TEST(test_a_bad_reply_is_unreadable_and_no_reply_unreachable);
    RUN_TEST(test_a_failed_read_keeps_the_time_of_the_last_good_one);
    RUN_TEST(test_the_first_read_is_15_s_after_the_start);
    RUN_TEST(test_reads_repeat_after_10_min_when_good_and_2_min_when_failed);
    RUN_TEST(test_a_change_of_address_or_source_makes_a_read_due_at_once);
    RUN_TEST(test_never_while_busy_or_in_a_safety_poll_pass);
    RUN_TEST(test_never_when_the_safety_poll_is_due_within_a_read);
    RUN_TEST(test_never_when_not_following_sqmeter);
    RUN_TEST(test_a_manual_location_is_never_saved_over);
    RUN_TEST(test_a_failed_read_is_never_saved);
    RUN_TEST(test_the_first_good_read_is_saved_with_its_time);
    RUN_TEST(test_a_jitter_under_a_thousandth_of_a_degree_is_not_saved);
    RUN_TEST(test_a_move_of_a_thousandth_of_a_degree_is_saved);
    RUN_TEST(test_a_change_of_the_gps_flag_is_saved);
    RUN_TEST(test_without_a_clock_the_time_is_zero);
    RUN_TEST(test_a_save_keeps_the_live_location_while_both_follow_sqmeter);
    RUN_TEST(test_the_status_names_the_source_age_and_problem);
    RUN_TEST(test_the_status_before_any_read_and_without_an_address);
    RUN_TEST(test_a_manual_location_has_no_sqmeter_status);
    RUN_TEST(test_the_site_document_of_a_current_sqmeter);
    RUN_TEST(test_an_unmasked_secret_is_only_ever_reported_as_set);
    RUN_TEST(test_a_v0_1_sqmeter_has_its_time_zone_at_the_top_and_nothing_else);
    RUN_TEST(test_a_location_not_set_at_sqmeter_has_no_coordinates);
    RUN_TEST(test_a_document_that_isnt_sqmeters_settings_is_refused);
    RUN_TEST(test_a_bad_mqtt_port_reads_as_the_default);
    RUN_TEST(test_the_filter_drops_everything_else);
    RUN_TEST(test_a_401_or_403_says_whether_a_password_is_wanted_or_refused);
    RUN_TEST(test_the_copy_sends_the_saved_login_only_to_its_own_host);
    return UNITY_END();
}
