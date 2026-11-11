#include <unity.h>

#include <ArduinoJson.h>

#include "DeviceCore.h"

#include <fstream>
#include <set>
#include <sstream>

// The device core: alert wording and levels from settings, the imaging-app
// watch documents, the send schedule and test alerts.

using namespace SQM;

namespace
{
    const std::vector<Alpaca::DeviceInfo> ONE_DOME = {{"Dome", "dome", 0, "RoofRoof Dome", "Roll-off roof", 3}};

    // The firmware's path: the Router's counters through a ClientWatch into
    // the alert inputs.
    struct Rig
    {
        const Config &cfg;
        Alpaca::ClientWatch watch;
        Alpaca::DeviceActivity activity[Alpaca::MAX_DEVICES];

        Alerts::AlertInputs at(uint32_t nowMs, bool poll)
        {
            if (poll)
                ++activity[0].requests;
            uint32_t silence[Alpaca::MAX_DEVICES];
            Core::clientSilenceMs(cfg, silence);
            watch.update(activity, silence, true, nowMs);
            Alerts::AlertInputs in;
            in.nowSeconds = nowMs / 1000;
            Core::addClientInputs(in, {watch, ONE_DOME, nowMs}, cfg, "23:01");
            return in;
        }
    };
} // namespace

void setUp() {}
void tearDown() {}

void test_alert_wording_and_levels()
{
    Config cfg = Config::createDefault();
    cfg.alerts.clientLost.title = "{device}: {event} at {time}";
    cfg.alerts.clientLost.message = "Quiet for {silent_for}, last {last_checked}";
    const Core::AlertSources sources{cfg};
    Alerts::AlertEngine engine;
    Rig rig{cfg, {}, {}};
    rig.activity[0].connected = true;
    rig.at(0, false);
    Core::runAlerts(engine, rig.at(1000, true), Core::alertRules(cfg), sources, {"23:00", "2026-10-07"});
    const Core::AlertStep step = Core::runAlerts(engine, rig.at(131000, false), Core::alertRules(cfg), sources, {"23:01", "2026-10-07"});
    TEST_ASSERT_EQUAL(1, step.outgoing.size());
    const Alerts::Alert &alert = step.outgoing[0];
    TEST_ASSERT_EQUAL(static_cast<int>(Alerts::AlertLevel::Urgent), static_cast<int>(alert.level));
    // {device} is the Alpaca device here: the event's own value wins.
    TEST_ASSERT_EQUAL_STRING("RoofRoof Dome: client_lost at 23:01", alert.title.c_str());
    TEST_ASSERT_EQUAL_STRING("Quiet for 2 min, last 22:59", alert.message.c_str());
    TEST_ASSERT_TRUE(alert.vars.empty()); // cleared once applied

    // An event switched Off never goes out.
    cfg.alerts.clientBack.level = 0;
    const Core::AlertStep quiet = Core::runAlerts(engine, rig.at(140000, true), Core::alertRules(cfg), sources, {"23:02", "2026-10-07"});
    TEST_ASSERT_EQUAL(0, quiet.outgoing.size());
}

// SQMeter spec 008 FR-006: the device fills exactly the common variables the
// web UI offers (lib/AlertLogic/template-variables.json).
void test_alert_vars_match_the_shared_list()
{
    std::ifstream in("lib/AlertLogic/template-variables.json");
    std::stringstream text;
    text << in.rdbuf();
    DynamicJsonDocument doc(4096);
    TEST_ASSERT_FALSE(deserializeJson(doc, text.str()));
    std::set<std::string> shared;
    for (JsonVariantConst name : doc["common"].as<JsonArrayConst>())
        shared.insert(name.as<std::string>());

    const Config cfg = Config::createDefault();
    Alerts::Alert alert; // no event values of its own
    std::set<std::string> filled;
    for (const auto &v : Core::alertVars(Core::AlertSources{cfg}, alert, Core::LocalClock{"23:00", "2026-10-07"}))
        filled.insert(v.first);
    TEST_ASSERT_TRUE_MESSAGE(filled == shared, "Core::alertVars and template-variables.json \"common\" differ");
}

void test_iso_utc_and_durations()
{
    TEST_ASSERT_EQUAL_STRING("2026-10-08T00:00:00Z", Core::isoUtc(1791417600).c_str());
    TEST_ASSERT_EQUAL_STRING("", Core::isoUtc(1000).c_str());
    TEST_ASSERT_EQUAL_STRING("45 s", Core::formatDuration(45).c_str());
    TEST_ASSERT_EQUAL_STRING("2 min", Core::formatDuration(120).c_str());
    TEST_ASSERT_EQUAL_STRING("1 h", Core::formatDuration(3600).c_str());
    TEST_ASSERT_EQUAL_STRING("1 h 5 min", Core::formatDuration(3900).c_str());
}

// /api/status "alpaca": one entry per Alpaca device, keyed by its type.
void test_client_watch_document()
{
    Config cfg = Config::createDefault();
    cfg.alpaca.enabled = true;
    DynamicJsonDocument doc(1024);
    Core::writeClientWatch(doc.to<JsonObject>(), Alpaca::ClientWatch{}, ONE_DOME, cfg, 1000);
    TEST_ASSERT_TRUE(doc["enabled"].as<bool>());
    TEST_ASSERT_EQUAL(1, doc["clients"].size());
    TEST_ASSERT_FALSE(doc["clients"]["dome"]["watching"].as<bool>());
    TEST_ASSERT_TRUE(doc["clients"]["dome"]["lastCheckedAgeMs"].isNull());

    DynamicJsonDocument none(256);
    Core::writeClientWatch(none.to<JsonObject>(), Alpaca::ClientWatch{}, {}, cfg, 1000);
    TEST_ASSERT_EQUAL(0, none["clients"].size());
}

// D-12: "only while an imaging app is connected" needs Alpaca.
void test_send_mode_needs_alpaca()
{
    Config cfg = Config::createDefault();
    cfg.alerts.sendMode = AlertsConfig::SendMode::WhileConnected;
    TEST_ASSERT_EQUAL(Alerts::SendMode::Any, Core::effectiveSendMode(cfg));
    cfg.alpaca.enabled = true;
    TEST_ASSERT_EQUAL(Alerts::SendMode::WhileConnected, Core::effectiveSendMode(cfg));
}

void test_test_alerts()
{
    const Config cfg = Config::createDefault();
    const Core::AlertSources sources{cfg};
    const Alerts::Alert generic = Core::buildTestAlert(nullptr, {}, sources, {"23:00", "2026-10-07"});
    TEST_ASSERT_EQUAL_STRING("Test notification", generic.title.c_str());
    TEST_ASSERT_EQUAL_STRING("Alerts from this RoofRoof are working.", generic.message.c_str());

    const Core::SampleAlert *sample = Core::sampleAlert("client_lost");
    TEST_ASSERT_NOT_NULL(sample);
    TEST_ASSERT_NULL(Core::sampleAlert("rain_started"));
    const Alerts::Alert lost = Core::buildTestAlert(sample, {4, "siren", "{device} quiet", ""}, sources, {"23:00", "2026-10-07"});
    TEST_ASSERT_EQUAL_STRING("Test: RoofRoof Dome (example) quiet", lost.title.c_str());
    TEST_ASSERT_EQUAL(static_cast<int>(Alerts::AlertLevel::Wake), static_cast<int>(lost.level));
    TEST_ASSERT_EQUAL_STRING("siren", lost.sound.c_str());
}

void test_status_document_fits();

// Spec 011 FR-026: bench alerts say so, once; the standard build's don't change.
void test_bench_alerts_are_labelled_once(void)
{
    Alerts::Alert alert;
    alert.title = "Roof didn't close";
    alert.message = "unchanged";
    TEST_ASSERT_EQUAL_STRING("Roof didn't close", Core::benchLabelled(alert, false).title.c_str());
    const Alerts::Alert bench = Core::benchLabelled(alert, true);
    TEST_ASSERT_EQUAL_STRING("Bench: Roof didn't close", bench.title.c_str());
    TEST_ASSERT_EQUAL_STRING("unchanged", bench.message.c_str());
    TEST_ASSERT_EQUAL_STRING("Bench: Roof didn't close", Core::benchLabelled(bench, true).title.c_str());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_bench_alerts_are_labelled_once);
    RUN_TEST(test_alert_wording_and_levels);
    RUN_TEST(test_alert_vars_match_the_shared_list);
    RUN_TEST(test_iso_utc_and_durations);
    RUN_TEST(test_client_watch_document);
    RUN_TEST(test_send_mode_needs_alpaca);
    RUN_TEST(test_test_alerts);
    RUN_TEST(test_status_document_fits);
    return UNITY_END();
}
