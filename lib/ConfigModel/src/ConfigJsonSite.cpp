#include "Config.h"
#include "ConfigInternal.h"
#include <ArduinoJson.h>

// Spec 020: the settings that follow the SQMeter at the same site, in
// (Config::applyJson): the location's source and the device's kept location,
// and the follow switches of the shared groups with their upgrade rule.

namespace SQM::ConfigDetail
{
    // Spec 020. A section without a source (saved before spec 020, or an
    // older page) is Manual when a location is set, else Sqmeter: a typed
    // location never starts following SQMeter. From the UI/API, while the
    // source is and stays SQMeter the device keeps its own location (a page
    // opened before the last read can't put an older one back), and the
    // device-owned "sqmeter" fields are never read.
    // The source sent, or the rule for a section without one. False on a
    // name that isn't one of the two, from the UI/API.
    bool applyLocationSource(const Section &location, LocationConfig &loc, std::string *error)
    {
        const std::string source = location["source"].is<const char *>() ? location["source"].as<const char *>() : "";
        if (source == "sqmeter" || source == "manual")
            loc.source = source == "manual" ? LocationConfig::Source::Manual : LocationConfig::Source::Sqmeter;
        else if (location.reader().strict() && location.has("source"))
            return setError(error, "Location: source must be sqmeter or manual");
        else if (location.has("set"))
            loc.source = loc.set ? LocationConfig::Source::Manual : LocationConfig::Source::Sqmeter;
        return true;
    }

    bool applyLocation(const Section &location, LocationConfig &loc, std::string *error)
    {
        if (location.isNull())
            return true;
        const LocationConfig before = loc;
        const bool strict = location.reader().strict();
        location.take("set", loc.set);
        location.take("latitude", loc.latitude);
        location.take("longitude", loc.longitude);
        if (!strict)
        {
            const Section kept = location.child("sqmeter");
            kept.take("gps", loc.gps);
            kept.take("savedAt", loc.savedAt);
        }
        if (!applyLocationSource(location, loc, error))
            return false;
        // With nothing kept yet there is nothing stale to protect: a restored
        // backup's location then stands until SQMeter answers.
        const bool deviceOwns = before.set && before.source == LocationConfig::Source::Sqmeter;
        if (!strict || !deviceOwns || loc.source != LocationConfig::Source::Sqmeter)
        {
            // A location sent from the UI/API that stands was typed, not read from SQMeter.
            if (strict && (loc.latitude != before.latitude || loc.longitude != before.longitude || loc.set != before.set))
            {
                loc.gps = false;
                loc.savedAt = 0;
            }
            return true;
        }
        // What was sent is still checked, so a wrong value is never accepted silently.
        if (!validateLocation(loc, error))
            return false;
        loc = before;
        return true;
    }

    // Spec 020: the follow switches (syncedAt only from stored settings).
    void applySync(const Section &sync, SqmeterSyncConfig &s)
    {
        const std::pair<const char *, SqmeterSyncConfig::Group *> groups[] = {{"time", &s.time}, {"mqtt", &s.mqtt}, {"alerts", &s.alerts}};
        for (const auto &group : groups)
        {
            const Section in = sync.child(group.first);
            in.take("follow", group.second->follow);
            // Made local from the UI/API: the device no longer owns it.
            if (sync.reader().strict() && !group.second->follow)
                group.second->syncedAt = 0;
            if (!sync.reader().strict())
                in.take("syncedAt", group.second->syncedAt);
        }
    }

    namespace
    {
        // FNV-1a over a group's values, each ended by a separator: equal values hash equal, and a
        // change is told apart without keeping copies of the strings (passwords among them).
        struct Hash
        {
            uint32_t h = 2166136261u;
            Hash &operator<<(const std::string &value)
            {
                for (const char c : value)
                    h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
                h = (h ^ 0x1fu) * 16777619u;
                return *this;
            }
            Hash &operator<<(uint32_t value) { return *this << std::to_string(value); }
        };
    } // namespace

    SyncedValues syncedValues(const Config &c)
    {
        const MQTTConfig &m = c.mqtt;
        const AlertsConfig &a = c.alerts;
        return SyncedValues{{
            (Hash() << c.ntp.timezone << c.ntp.server1 << c.ntp.server2).h,
            (Hash() << m.broker << m.port << m.username << m.password << m.enabled << c.safety.mqttTopic).h,
            (Hash() << a.ntfyServer << a.ntfyTopic << a.ntfyToken << a.ntfyEnabled << a.pushoverUserKey << a.pushoverAppToken
                    << a.pushoverEnabled)
                .h,
        }};
    }

    uint8_t typedOver(const RoofConfig &a, const RoofConfig &b)
    {
        uint8_t bits = 0;
        if (a.travelMm != b.travelMm)
            bits |= RoofConfig::MEASURED_TRAVEL;
        if (a.speedMmPerSecond != b.speedMmPerSecond)
            bits |= RoofConfig::MEASURED_SPEED;
        if (a.timeoutMarginSeconds != b.timeoutMarginSeconds)
            bits |= RoofConfig::MEASURED_MARGIN;
        if (a.hallHighEnd != b.hallHighEnd || a.hallHighCounts != b.hallHighCounts || a.hallLowCounts != b.hallLowCounts)
            bits |= RoofConfig::MEASURED_HALL;
        if (a.openDirection != b.openDirection)
            bits |= RoofConfig::MEASURED_DIRECTION;
        return bits;
    }

    void unfollowEdited(const Section &sync, const SyncedValues &before, Config &cfg)
    {
        const SyncedValues after = syncedValues(cfg);
        const char *const names[] = {"time", "mqtt", "alerts"};
        SqmeterSyncConfig::Group *const groups[] = {&cfg.sqmeterSync.time, &cfg.sqmeterSync.mqtt, &cfg.sqmeterSync.alerts};
        for (size_t i = 0; i < 3; ++i)
        {
            if (!groups[i]->follow || sync.child(names[i]).has("follow") || after.group[i] == before.group[i])
                continue;
            groups[i]->follow = false;
            groups[i]->syncedAt = 0;
        }
    }

    // Settings saved before spec 020: a group whose values the person had
    // already set stays local; one still at its defaults follows SQMeter.
    void migrateSync(Config &cfg)
    {
        const Config d = Config::createDefault();
        cfg.sqmeterSync.time.follow =
            cfg.ntp.timezone == d.ntp.timezone && cfg.ntp.server1 == d.ntp.server1 && cfg.ntp.server2 == d.ntp.server2;
        cfg.sqmeterSync.mqtt.follow = cfg.mqtt.broker.empty() && cfg.mqtt.username.empty() && cfg.mqtt.port == d.mqtt.port &&
                                      cfg.safety.mqttTopic == d.safety.mqttTopic;
        cfg.sqmeterSync.alerts.follow = cfg.alerts.ntfyTopic.empty() && cfg.alerts.ntfyServer == d.alerts.ntfyServer;
    }

} // namespace SQM::ConfigDetail
