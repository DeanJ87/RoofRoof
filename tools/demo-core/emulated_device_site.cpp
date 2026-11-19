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
} // namespace

void EmulatedDevice::resetSite()
{
    siteSchedule = Site::LocationSchedule();
    siteRead = Site::LocationRead();
}

// SafetyPoller::run's location read and main.cpp handleSite's save, in one pass.
void EmulatedDevice::runSite()
{
    if (cfg.safety.httpUrl != siteAddress || cfg.location.source != siteSource)
    {
        siteAddress = cfg.safety.httpUrl;
        siteSource = cfg.location.source;
        ++siteGeneration;
        siteRead = Site::LocationRead();
    }
    const bool busy = !platformHold("flashWrite").empty();
    Site::LocationSchedule::Pass pass;
    pass.nowMs = nowMs;
    pass.enabled = cfg.location.source == LocationConfig::Source::Sqmeter && !Site::siteUrl(siteAddress, Site::STATUS_PATH).empty();
    pass.busy = busy;
    pass.generation = siteGeneration;
    if (siteSchedule.due(pass))
    {
        DynamicJsonDocument filter(Site::STATUS_FILTER_BYTES);
        Site::statusFilter(filter);
        DynamicJsonDocument doc(Site::STATUS_DOC_BYTES);
        Site::Fetch fetch;
        if (roof.injectors().sqmeterOffline)
            fetch.error = OFFLINE;
        else
        {
            fetch.status = 200;
            fetch.parsed = !deserializeJson(doc, DEMO_SQMETER_STATUS, DeserializationOption::Filter(filter));
        }
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

void EmulatedDevice::writeLocation(JsonObject out) const
{
    Site::writeLocationStatus(out, cfg.location, siteRead, !Site::siteUrl(cfg.safety.httpUrl, Site::STATUS_PATH).empty(), nowMs);
}

std::string EmulatedDevice::sqmeterSite(const std::string &body)
{
    StaticJsonDocument<512> in;
    std::string address = cfg.safety.httpUrl;
    if (!body.empty() && !deserializeJson(in, body) && in["url"].is<const char *>())
        address = in["url"].as<const char *>();
    const std::string url = Site::siteUrl(address, Site::CONFIG_PATH);
    if (address.empty())
        return response(400, errorJson("SQMeter's address isn't set"));
    if (url.empty())
        return response(400, errorJson("SQMeter's address must be an http:// address"));
    const std::string held = platformHold("flashWrite");
    if (!held.empty())
        return response(409, errorJson(held));
    if (roof.injectors().sqmeterOffline)
        return response(502, errorJson(std::string("SQMeter not answering: ") + OFFLINE));
    DynamicJsonDocument filter(Site::CONFIG_FILTER_BYTES);
    Site::configFilter(filter);
    DynamicJsonDocument doc(Site::CONFIG_DOC_BYTES);
    Site::SiteConfig site;
    std::string why;
    if (deserializeJson(doc, DEMO_SQMETER_CONFIG, DeserializationOption::Filter(filter)) || !Site::readSiteConfig(doc, site, why))
        return response(502, errorJson("SQMeter's settings couldn't be read: " + why));
    DynamicJsonDocument out(1536);
    Site::writeSiteDocument(out.to<JsonObject>(), address, site);
    return response(200, serialize(out));
}
