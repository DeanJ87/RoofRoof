#include "SqmeterSite.h"

#include <cmath>

#include "NetAddress.h"

// Spec 020: site data from SQMeter (see SqmeterSite.h).

namespace SQM
{
    namespace Site
    {
        namespace
        {
            // "" when the field is missing or not a string.
            std::string text(JsonVariantConst value)
            {
                return value.is<const char *>() ? value.as<const char *>() : "";
            }

            // A secret is "set" when SQMeter shows anything for it: its mask
            // ("********"), or the text itself from a release that didn't mask.
            bool secretSet(JsonVariantConst value)
            {
                return value.is<const char *>() && value.as<const char *>()[0] != '\0';
            }

            bool coordinate(JsonVariantConst value, double limit, double &out)
            {
                if (!value.is<double>() || value.is<bool>())
                    return false;
                out = value.as<double>();
                return std::isfinite(out) && out >= -limit && out <= limit;
            }

            LocationRead failed(Problem problem, const std::string &detail, uint64_t atMs)
            {
                LocationRead read;
                read.problem = problem;
                read.detail = detail;
                read.atMs = atMs;
                return read;
            }

            void readTime(const JsonDocument &doc, SiteConfig &out)
            {
                JsonVariantConst ntp = doc["ntp"];
                if (ntp.is<JsonObjectConst>())
                {
                    out.hasTime = true;
                    out.timezone = text(ntp["timezone"]);
                    out.server1 = text(ntp["server1"]);
                    out.server2 = text(ntp["server2"]);
                }
                // v0.1 to v0.2.0-beta.1 kept the time zone at the top (research R5).
                if (out.timezone.empty() && doc["timezone"].is<const char *>())
                {
                    out.hasTime = true;
                    out.timezone = text(doc["timezone"]);
                }
            }

            void readMqtt(JsonVariantConst mqtt, SiteConfig &out)
            {
                if (!mqtt.is<JsonObjectConst>())
                    return;
                out.hasMqtt = true;
                out.broker = text(mqtt["broker"]);
                const long port = mqtt["port"].is<long>() ? mqtt["port"].as<long>() : 0;
                out.port = port >= 1 && port <= 65535 ? static_cast<uint16_t>(port) : 1883;
                out.username = text(mqtt["username"]);
                out.topic = text(mqtt["topic"]);
                out.passwordSet = secretSet(mqtt["password"]);
            }

            void readAlerts(JsonVariantConst alerts, SiteConfig &out)
            {
                JsonVariantConst ntfy = alerts["ntfy"];
                if (ntfy.is<JsonObjectConst>())
                {
                    out.hasNtfy = true;
                    out.ntfyServer = text(ntfy["server"]);
                    out.ntfyTopic = text(ntfy["topic"]);
                    out.ntfyTokenSet = secretSet(ntfy["token"]);
                }
                JsonVariantConst pushover = alerts["pushover"];
                if (pushover.is<JsonObjectConst>())
                {
                    out.hasPushover = true;
                    out.pushoverUserKeySet = secretSet(pushover["userKey"]);
                    out.pushoverAppTokenSet = secretSet(pushover["appToken"]);
                }
            }

            void writeParts(JsonObject out, const SiteConfig &site)
            {
                if (site.hasMqtt)
                {
                    JsonObject mqtt = out.createNestedObject("mqtt");
                    mqtt["broker"] = site.broker;
                    mqtt["port"] = site.port;
                    mqtt["username"] = site.username;
                    mqtt["topic"] = site.topic;
                    mqtt["passwordSet"] = site.passwordSet;
                }
                else
                    out["mqtt"] = nullptr;
                if (site.hasNtfy)
                {
                    JsonObject ntfy = out.createNestedObject("ntfy");
                    ntfy["server"] = site.ntfyServer;
                    ntfy["topic"] = site.ntfyTopic;
                    ntfy["tokenSet"] = site.ntfyTokenSet;
                }
                else
                    out["ntfy"] = nullptr;
                if (site.hasPushover)
                {
                    JsonObject pushover = out.createNestedObject("pushover");
                    pushover["userKeySet"] = site.pushoverUserKeySet;
                    pushover["appTokenSet"] = site.pushoverAppTokenSet;
                }
                else
                    out["pushover"] = nullptr;
            }
        } // namespace

        std::string siteOrigin(const std::string &address)
        {
            Net::HttpUrl url;
            if (address.empty() || Net::parseHttpUrl(address, url) != Net::UrlError::None || url.https)
                return "";
            std::string origin = "http://" + Net::hostForUrl(url.host);
            if (url.port != 80)
                origin += ":" + std::to_string(url.port);
            return origin;
        }

        std::string siteUrl(const std::string &address, const char *path)
        {
            const std::string origin = siteOrigin(address);
            return origin.empty() ? origin : origin + path;
        }

        const char *problemName(Problem problem)
        {
            switch (problem)
            {
            case Problem::Waiting:
                return "waiting";
            case Problem::NoAddress:
                return "noAddress";
            case Problem::Unreachable:
                return "unreachable";
            case Problem::Unreadable:
                return "unreadable";
            case Problem::NoLocation:
                return "noLocation";
            default:
                return nullptr;
            }
        }

        void statusFilter(JsonDocument &filter)
        {
            JsonObject sky = filter.createNestedObject("sky");
            sky["latitude"] = true;
            sky["longitude"] = true;
            sky["locationSource"] = true;
        }

        LocationRead readLocation(const Fetch &fetch, const JsonDocument &doc, uint64_t atMs)
        {
            if (fetch.status <= 0)
                return failed(Problem::Unreachable, fetch.error.empty() ? "no reply" : fetch.error, atMs);
            if (fetch.status != 200)
                return failed(Problem::Unreadable, "HTTP " + std::to_string(fetch.status), atMs);
            if (!fetch.parsed || !doc.is<JsonObjectConst>())
                return failed(Problem::Unreadable, "not SQMeter's status", atMs);
            JsonVariantConst sky = doc["sky"];
            const std::string source = text(sky["locationSource"]);
            if (!sky.is<JsonObjectConst>() || (source != "gps" && source != "manual"))
                return failed(Problem::NoLocation, "SQMeter has no location", atMs);
            LocationRead read;
            if (!coordinate(sky["latitude"], 90.0, read.latitude) || !coordinate(sky["longitude"], 180.0, read.longitude))
                return failed(Problem::Unreadable, "location out of range", atMs);
            read.problem = Problem::None;
            read.gps = source == "gps";
            read.atMs = atMs;
            read.goodAtMs = atMs;
            return read;
        }

        void record(LocationRead &kept, const LocationRead &fresh)
        {
            const uint64_t lastGood = kept.goodAtMs;
            kept = fresh;
            if (!fresh.good())
                kept.goodAtMs = lastGood;
        }

        bool LocationSchedule::due(const Pass &pass)
        {
            if (!pass.enabled)
                return false;
            if (!started || pass.generation != seenGeneration)
            {
                nextMs = started ? pass.nowMs : pass.nowMs + FIRST_MS;
                started = true;
                seenGeneration = pass.generation;
            }
            return !pass.busy && !pass.safetyPolled && pass.nowMs >= nextMs;
        }

        void LocationSchedule::done(bool good, uint64_t nowMs)
        {
            nextMs = nowMs + (good ? GOOD_MS : FAILED_MS);
        }

        std::optional<LocationConfig> locationToSave(
            const LocationConfig &current, const LocationRead &read, int64_t epochSeconds, bool clockSet)
        {
            if (current.source != LocationConfig::Source::Sqmeter || !read.good())
                return std::nullopt;
            const bool moved = std::fabs(read.latitude - current.latitude) >= SAVE_STEP_DEG ||
                               std::fabs(read.longitude - current.longitude) >= SAVE_STEP_DEG;
            if (current.set && current.gps == read.gps && !moved)
                return std::nullopt;
            LocationConfig next = current;
            next.set = true;
            next.latitude = read.latitude;
            next.longitude = read.longitude;
            next.gps = read.gps;
            next.savedAt = clockSet && epochSeconds > 0 && epochSeconds <= INT64_C(0xFFFFFFFF) ? static_cast<uint32_t>(epochSeconds) : 0;
            return next;
        }

        void writeLocationStatus(JsonObject out, const LocationConfig &config, const LocationRead &read, bool addressSet, uint64_t nowMs)
        {
            const bool sqmeter = config.source == LocationConfig::Source::Sqmeter;
            out["source"] = sqmeter ? "sqmeter" : "manual";
            out["set"] = config.set;
            out["gps"] = sqmeter && config.set && config.gps;
            if (sqmeter && config.savedAt != 0)
                out["savedAt"] = config.savedAt;
            else
                out["savedAt"] = nullptr;
            if (sqmeter && read.goodAtMs != 0 && nowMs >= read.goodAtMs)
                out["readAgeSeconds"] = static_cast<uint32_t>((nowMs - read.goodAtMs) / 1000);
            else
                out["readAgeSeconds"] = nullptr;
            const char *problem = !sqmeter ? nullptr : !addressSet ? problemName(Problem::NoAddress) : problemName(read.problem);
            if (problem != nullptr)
                out["problem"] = problem;
            else
                out["problem"] = nullptr;
        }

        void configFilter(JsonDocument &filter)
        {
            filter["deviceName"] = true;
            filter["timezone"] = true;
            JsonObject location = filter.createNestedObject("location");
            location["set"] = true;
            location["latitude"] = true;
            location["longitude"] = true;
            filter["ntp"]["timezone"] = true;
            filter["ntp"]["server1"] = true;
            filter["ntp"]["server2"] = true;
            for (const char *key : {"broker", "port", "username", "topic", "password"})
                filter["mqtt"][key] = true;
            for (const char *key : {"server", "topic", "token"})
                filter["alerts"]["ntfy"][key] = true;
            filter["alerts"]["pushover"]["userKey"] = true;
            filter["alerts"]["pushover"]["appToken"] = true;
        }

        bool readSiteConfig(const JsonDocument &doc, SiteConfig &out, std::string &why)
        {
            out = SiteConfig{};
            if (!doc.is<JsonObjectConst>() || !doc["deviceName"].is<const char *>())
            {
                why = "not SQMeter's settings";
                return false;
            }
            out.name = text(doc["deviceName"]);
            JsonVariantConst location = doc["location"];
            if (location.is<JsonObjectConst>())
            {
                out.hasLocation = true;
                out.locationSet = location["set"].as<bool>() && coordinate(location["latitude"], 90.0, out.latitude) &&
                                  coordinate(location["longitude"], 180.0, out.longitude);
                if (!out.locationSet)
                    out.latitude = out.longitude = 0.0;
            }
            readTime(doc, out);
            readMqtt(doc["mqtt"], out);
            readAlerts(doc["alerts"], out);
            return true;
        }

        void writeSiteDocument(JsonObject out, const std::string &address, const SiteConfig &site)
        {
            out["address"] = siteOrigin(address);
            out.createNestedObject("device")["name"] = site.name;
            if (site.hasLocation)
            {
                JsonObject location = out.createNestedObject("location");
                location["set"] = site.locationSet;
                location["latitude"] = site.latitude;
                location["longitude"] = site.longitude;
            }
            else
                out["location"] = nullptr;
            if (site.hasTime)
            {
                JsonObject time = out.createNestedObject("time");
                time["timezone"] = site.timezone;
                time["server1"] = site.server1;
                time["server2"] = site.server2;
            }
            else
                out["time"] = nullptr;
            writeParts(out, site);
        }
    } // namespace Site
} // namespace SQM
