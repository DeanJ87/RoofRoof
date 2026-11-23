// EmulatedDevice: site data from the simulated SQMeter (spec 020). The
// firmware's own schedule, parsers and save rule (lib/SafetyInputLogic
// SqmeterSite.h); only the fetch is simulated.

#include "emulated_device.h"

using namespace DemoCore;

namespace
{
    // The simulated SQMeter: a GPS fix at the Royal Observatory, Greenwich.
    const char *const DEMO_SQMETER_STATUS = R"({"sky":{"locationSource":"gps","nightKnown":true,"latitude":51.4769,"longitude":-0.0005}})";
    // Its settings as GET /api/config shows them (secrets masked), cut down.
    const char *const DEMO_SQMETER_CONFIG =
        R"({"deviceName":"SQMeter","language":"en","wifi":{"ssid":"DarkSkyLab","password":"********","hostname":"sqmeter"},)"
        R"("mqtt":{"enabled":true,"broker":"192.168.1.10","port":1883,"username":"observatory","password":"********","topic":"sqmeter"},)"
        R"("ntp":{"enabled":true,"server1":"uk.pool.ntp.org","server2":"time.cloudflare.com","syncIntervalMs":600000,"timezone":"GMT0BST,M3.5.0/1,M10.5.0"},)"
        R"("location":{"set":false,"latitude":0,"longitude":0,"showSunMoon":true},)"
        R"("alerts":{"enabled":true,"ntfy":{"enabled":true,"server":"https://ntfy.sh","token":"","topic":"darkskylab-sqm"},)"
        R"("pushover":{"enabled":false,"userKey":"","appToken":"","sound":""}}})";
    const char *const OFFLINE = "connection refused (demo: SQMeter offline)";

    // SafetyPoller::fetchJson on the simulated SQMeter: offline, locked (401
    // unless the login is its own), or its document through the filter.
    Site::Fetch demoFetch(
        const DemoRoof::Injectors &inject, const Site::Endpoint &from, const char *body, const JsonDocument &filter, JsonDocument &doc)
    {
        Site::Fetch fetch;
        fetch.credentials = from.sendsPassword();
        fetch.status = DemoRoof::demoSqmeterAnswer(inject, from.username, from.password);
        if (fetch.status == 0)
            fetch.error = OFFLINE;
        else if (fetch.status == 200)
            fetch.parsed = !deserializeJson(doc, body, DeserializationOption::Filter(filter));
        return fetch;
    }
} // namespace

void EmulatedDevice::resetSite()
{
    siteSchedule = Site::LocationSchedule();
    siteRead = Site::LocationRead();
    settingsSchedule = Site::LocationSchedule();
    settingsRead = Site::ConfigRead();
    syncState = Site::SyncState();
}

// The settings read (SafetyPoller::pollSettings) and the sync (main.cpp
// syncSiteSettings): the firmware's own rules, one read per pass.
void EmulatedDevice::runSettingsSync(bool busy)
{
    Site::LocationSchedule::Pass pass;
    pass.nowMs = nowMs;
    pass.enabled = settingsFollow && !Site::siteUrl(siteAddress, Site::CONFIG_PATH).empty();
    pass.busy = busy;
    pass.generation = siteGeneration;
    if (settingsSchedule.due(pass))
    {
        DynamicJsonDocument filter(Site::CONFIG_FILTER_BYTES);
        Site::configFilter(filter);
        DynamicJsonDocument doc(Site::CONFIG_DOC_BYTES);
        const Site::Endpoint from{"", cfg.safety.httpUsername, cfg.safety.httpPassword};
        const Site::Fetch fetch = demoFetch(roof.injectors(), from, DEMO_SQMETER_CONFIG, filter, doc);
        const Site::ConfigRead fresh = Site::readConfig(fetch, doc, nowMs);
        Site::record(settingsRead, fresh);
        settingsSchedule.done(fresh.good(), nowMs);
        if (fresh.good() && !busy)
        {
            Site::SyncOutcome out;
            Site::syncFromSite(out, cfg, settingsRead.site, epoch, validEpoch() != 0);
            for (size_t i = 0; i < Site::GROUP_COUNT; ++i)
                syncState.refused[i] = out.groups[i].refused;
            if (out.any())
            {
                Site::applyOutcome(out, cfg);
                if (roofBooted)
                    roof.settingsChanged(cfg);
            }
        }
    }
}

// SafetyPoller::run's location read and main.cpp handleSite's save, in one pass.
void EmulatedDevice::runSite()
{
    const SqmeterSyncConfig &sync = cfg.sqmeterSync;
    const bool follows = sync.time.follow || sync.mqtt.follow || sync.alerts.follow;
    const std::string login = cfg.safety.httpUsername + '\n' + cfg.safety.httpPassword;
    if (cfg.safety.httpUrl != siteAddress || cfg.location.source != siteSource || follows != settingsFollow || login != siteLogin)
    {
        siteLogin = login;
        siteAddress = cfg.safety.httpUrl;
        siteSource = cfg.location.source;
        settingsFollow = follows;
        ++siteGeneration;
        siteRead = Site::LocationRead();
        settingsRead = Site::ConfigRead();
    }
    const bool busy = !platformHold("flashWrite").empty();
    Site::LocationSchedule::Pass pass;
    pass.nowMs = nowMs;
    pass.enabled = cfg.location.source == LocationConfig::Source::Sqmeter && !Site::siteUrl(siteAddress, Site::STATUS_PATH).empty();
    pass.busy = busy;
    pass.generation = siteGeneration;
    const bool locationDue = siteSchedule.due(pass);
    if (!locationDue)
        runSettingsSync(busy); // one site read per pass, as the poller
    if (locationDue)
    {
        DynamicJsonDocument filter(Site::STATUS_FILTER_BYTES);
        Site::statusFilter(filter);
        DynamicJsonDocument doc(Site::STATUS_DOC_BYTES);
        const Site::Endpoint from{"", cfg.safety.httpUsername, cfg.safety.httpPassword};
        const Site::Fetch fetch = demoFetch(roof.injectors(), from, DEMO_SQMETER_STATUS, filter, doc);
        const Site::LocationRead fresh = Site::readLocation(fetch, doc, nowMs);
        Site::record(siteRead, fresh);
        siteSchedule.done(fresh.good(), nowMs);
    }
    if (busy)
        return;
    const auto next = Site::locationToSave(cfg.location, siteRead, epoch, validEpoch() != 0);
    if (!next)
        return;
    Config updated = cfg;
    updated.location = *next;
    if (!updated.validate())
        return;
    cfg = updated;
    if (roofBooted)
        roof.settingsChanged(cfg);
}

void EmulatedDevice::writeSync(JsonObject out) const
{
    Site::writeSyncStatus(
        out, Site::SyncView{cfg, settingsRead, syncState, !Site::siteUrl(cfg.safety.httpUrl, Site::STATUS_PATH).empty(), nowMs});
}

void EmulatedDevice::writeLocation(JsonObject out) const
{
    Site::writeLocationStatus(out, cfg.location, siteRead, !Site::siteUrl(cfg.safety.httpUrl, Site::STATUS_PATH).empty(), nowMs);
}

// The body's typed address and login (WebServerSite.cpp addressFrom), or the
// saved ones; "" when the request may go ahead, else the refusal to send.
std::string EmulatedDevice::sqmeterRequest(const std::string &body, std::string &address, Site::TypedLogin &login)
{
    StaticJsonDocument<512> in;
    address = cfg.safety.httpUrl;
    if (!body.empty() && !deserializeJson(in, body))
    {
        if (in["url"].is<const char *>())
            address = in["url"].as<const char *>();
        if (in["username"].is<const char *>())
            login.username = in["username"].as<const char *>();
        if (in["password"].is<const char *>())
            login.password = in["password"].as<const char *>();
    }
    if (address.empty())
        return response(400, errorJson("SQMeter's address isn't set"));
    if (Site::siteUrl(address, Site::CONFIG_PATH).empty())
        return response(400, errorJson("SQMeter's address isn't valid: e.g. 192.168.1.50 or sqmeter.local"));
    const std::string held = platformHold("flashWrite");
    return held.empty() ? std::string() : response(409, errorJson(held));
}

// Spec 020 FR-038: POST /api/sqmeter/test, one read of the simulated SQMeter's /api/safety.
std::string EmulatedDevice::sqmeterTest(const std::string &body)
{
    std::string address;
    Site::TypedLogin login;
    const std::string refused = sqmeterRequest(body, address, login);
    if (!refused.empty())
        return refused;
    const Site::Endpoint to = Site::copyEndpoint(cfg.safety, address, login, Site::SAFETY_PATH);
    Safety::HttpOutcome outcome{DemoRoof::demoSqmeterAnswer(roof.injectors(), to.username, to.password), "", "", to.sendsPassword()};
    if (outcome.status == 0)
        outcome.error = OFFLINE;
    else if (outcome.status == 200)
    {
        const Safety::SqmeterReply reply = roof.simulatedSafety();
        StaticJsonDocument<128> doc;
        doc["safe"] = reply.safe;
        doc["reasonFlags"] = reply.reasonFlags;
        doc["evaluatedAgeMs"] = 5;
        outcome.body = serialize(doc);
    }
    StaticJsonDocument<256> out;
    Safety::writeTestResult(out.to<JsonObject>(), outcome);
    return response(out["error"].isNull() ? 200 : 502, serialize(out));
}

std::string EmulatedDevice::sqmeterSite(const std::string &body)
{
    std::string address;
    Site::TypedLogin login;
    const std::string refused = sqmeterRequest(body, address, login);
    if (!refused.empty())
        return refused;
    DynamicJsonDocument filter(Site::CONFIG_FILTER_BYTES);
    Site::configFilter(filter);
    DynamicJsonDocument doc(Site::CONFIG_DOC_BYTES);
    const Site::Fetch fetch = demoFetch(roof.injectors(), Site::copyEndpoint(cfg.safety, address, login), DEMO_SQMETER_CONFIG, filter, doc);
    if (fetch.status == 0)
        return response(502, errorJson(std::string("SQMeter not answering: ") + OFFLINE));
    if (const char *refused = Safety::loginRefusalText(fetch.status, fetch.credentials))
        return response(502, errorJson(refused));
    Site::SiteConfig site;
    std::string why = "not SQMeter's settings";
    if (!fetch.parsed || !Site::readSiteConfig(doc, site, why))
        return response(502, errorJson("SQMeter's settings couldn't be read: " + why));
    DynamicJsonDocument out(1536);
    Site::writeSiteDocument(out.to<JsonObject>(), address, site);
    return response(200, serialize(out));
}
