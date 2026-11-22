#include "SqmeterSync.h"

// Spec 020: keeping the shared settings in step with SQMeter (see SqmeterSync.h).

namespace SQM
{
    namespace Site
    {
        namespace
        {
            constexpr Group GROUPS[GROUP_COUNT] = {Group::Time, Group::Mqtt, Group::Alerts};

            ConfigRead failedRead(Problem problem, const std::string &detail, uint64_t atMs)
            {
                ConfigRead read;
                read.problem = problem;
                read.detail = detail;
                read.atMs = atMs;
                return read;
            }

            bool applyTime(Config &c, const SiteConfig &site)
            {
                if (!site.hasTime)
                    return false;
                bool taken = false;
                if (!site.timezone.empty())
                {
                    c.ntp.timezone = site.timezone;
                    taken = true;
                }
                if (!site.server1.empty() || !site.server2.empty())
                {
                    c.ntp.server1 = site.server1;
                    c.ntp.server2 = site.server2;
                    taken = true;
                }
                return taken;
            }

            bool applyMqtt(Config &c, const SiteConfig &site)
            {
                if (!site.hasMqtt || site.broker.empty())
                    return false;
                const bool newServer = site.broker != c.mqtt.broker || site.port != c.mqtt.port || site.username != c.mqtt.username;
                c.mqtt.broker = site.broker;
                c.mqtt.port = site.port;
                c.mqtt.username = site.username;
                if (newServer)
                    c.mqtt.password.clear(); // never sent to a broker it wasn't saved for
                if (!site.topic.empty())
                    c.safety.mqttTopic = site.topic; // where the MQTT weather source reads <base>/safe
                return true;
            }

            bool applyAlerts(Config &c, const SiteConfig &site)
            {
                if (!site.hasNtfy || site.ntfyTopic.empty())
                    return false;
                if (site.ntfyServer != c.alerts.ntfyServer)
                    c.alerts.ntfyToken.clear(); // never sent to a server it wasn't saved for
                c.alerts.ntfyServer = site.ntfyServer;
                c.alerts.ntfyTopic = site.ntfyTopic;
                return true;
            }

            bool applyGroup(Config &c, const SiteConfig &site, Group group)
            {
                switch (group)
                {
                case Group::Time:
                    return applyTime(c, site);
                case Group::Mqtt:
                    return applyMqtt(c, site);
                default:
                    return applyAlerts(c, site);
                }
            }

            // The group's own fields (not its secret, not its switch).
            bool sameGroup(const Config &a, const Config &b, Group group)
            {
                switch (group)
                {
                case Group::Time:
                    return a.ntp.timezone == b.ntp.timezone && a.ntp.server1 == b.ntp.server1 && a.ntp.server2 == b.ntp.server2;
                case Group::Mqtt:
                    return a.mqtt.broker == b.mqtt.broker && a.mqtt.port == b.mqtt.port && a.mqtt.username == b.mqtt.username &&
                           a.safety.mqttTopic == b.safety.mqttTopic;
                default:
                    return a.alerts.ntfyServer == b.alerts.ntfyServer && a.alerts.ntfyTopic == b.alerts.ntfyTopic;
                }
            }

            void copyGroup(const Config &from, Config &to, Group group)
            {
                switch (group)
                {
                case Group::Time:
                    to.ntp.timezone = from.ntp.timezone;
                    to.ntp.server1 = from.ntp.server1;
                    to.ntp.server2 = from.ntp.server2;
                    break;
                case Group::Mqtt:
                    to.mqtt.broker = from.mqtt.broker;
                    to.mqtt.port = from.mqtt.port;
                    to.mqtt.username = from.mqtt.username;
                    to.safety.mqttTopic = from.safety.mqttTopic;
                    break;
                default:
                    to.alerts.ntfyServer = from.alerts.ntfyServer;
                    to.alerts.ntfyTopic = from.alerts.ntfyTopic;
                    break;
                }
                groupOf(to.sqmeterSync, group).syncedAt = groupOf(from.sqmeterSync, group).syncedAt;
            }

            // One group's sync onto `out.next` (FR-026, FR-027).
            void syncGroup(SyncOutcome &out, const SiteConfig &site, Group group, uint32_t stamp)
            {
                GroupOutcome &result = out.groups[static_cast<size_t>(group)];
                if (group == Group::Mqtt && out.next.mqtt.roofCommands)
                {
                    Config probe = out.next;
                    if (applyMqtt(probe, site) && !sameGroup(probe, out.next, group))
                        result.refused = "MQTT roof commands are on: SQMeter's broker isn't taken; set it here";
                    return;
                }
                Config candidate = out.next;
                if (!applyGroup(candidate, site, group))
                    return; // SQMeter has nothing for this group
                SqmeterSyncConfig::Group &mark = groupOf(candidate.sqmeterSync, group);
                if (sameGroup(candidate, out.next, group))
                {
                    if (mark.syncedAt == 0)
                    {
                        groupOf(out.next.sqmeterSync, group).syncedAt = stamp;
                        out.marked = true;
                    }
                    return;
                }
                std::string why;
                if (!candidate.validateChange(&why))
                {
                    result.refused = std::string("SQMeter's value refused: ") + why;
                    return;
                }
                mark.syncedAt = stamp;
                out.next = candidate;
                result.changed = true;
            }
        } // namespace

        const char *groupName(Group group)
        {
            return group == Group::Time ? "time" : group == Group::Mqtt ? "mqtt" : "alerts";
        }

        SqmeterSyncConfig::Group &groupOf(SqmeterSyncConfig &sync, Group group)
        {
            return group == Group::Time ? sync.time : group == Group::Mqtt ? sync.mqtt : sync.alerts;
        }

        const SqmeterSyncConfig::Group &groupOf(const SqmeterSyncConfig &sync, Group group)
        {
            return group == Group::Time ? sync.time : group == Group::Mqtt ? sync.mqtt : sync.alerts;
        }

        ConfigRead readConfig(const Fetch &fetch, const JsonDocument &doc, uint64_t atMs)
        {
            if (fetch.status <= 0)
                return failedRead(Problem::Unreachable, fetch.error.empty() ? "no reply" : fetch.error, atMs);
            if (fetch.status != 200)
                return failedRead(Problem::Unreadable, "HTTP " + std::to_string(fetch.status), atMs);
            ConfigRead read;
            std::string why = "not SQMeter's settings";
            if (!fetch.parsed || !readSiteConfig(doc, read.site, why))
                return failedRead(Problem::Unreadable, why, atMs);
            read.problem = Problem::None;
            read.atMs = read.goodAtMs = atMs;
            return read;
        }

        void record(ConfigRead &kept, const ConfigRead &fresh)
        {
            if (fresh.good())
            {
                kept = fresh;
                return;
            }
            kept.problem = fresh.problem;
            kept.detail = fresh.detail;
            kept.atMs = fresh.atMs;
        }

        bool SyncOutcome::any() const
        {
            if (marked)
                return true;
            for (const GroupOutcome &g : groups)
                if (g.changed)
                    return true;
            return false;
        }

        std::string SyncOutcome::changedNames() const
        {
            std::string names;
            for (Group group : GROUPS)
                if (groups[static_cast<size_t>(group)].changed)
                    names += (names.empty() ? "" : ", ") + std::string(groupName(group));
            return names;
        }

        SyncOutcome syncFromSite(const Config &live, const SiteConfig &site, int64_t epochSeconds, bool clockSet)
        {
            SyncOutcome out;
            out.next = live;
            const uint32_t stamp =
                clockSet && epochSeconds > 0 && epochSeconds <= INT64_C(0xFFFFFFFF) ? static_cast<uint32_t>(epochSeconds) : 1;
            for (Group group : GROUPS)
                if (groupOf(live.sqmeterSync, group).follow)
                    syncGroup(out, site, group, stamp);
            return out;
        }

        void keepFollowed(const Config &live, Config &next)
        {
            keepDeviceLocation(live.location, next.location);
            for (Group group : GROUPS)
            {
                const SqmeterSyncConfig::Group &was = groupOf(live.sqmeterSync, group);
                if (was.follow && was.syncedAt != 0 && groupOf(next.sqmeterSync, group).follow)
                    copyGroup(live, next, group);
            }
        }

        std::vector<const char *> secretsNeeded(const Config &config, const SiteConfig &site, Group group)
        {
            std::vector<const char *> needs;
            if (!groupOf(config.sqmeterSync, group).follow)
                return needs;
            if (group == Group::Mqtt && site.hasMqtt && site.passwordSet && config.mqtt.password.empty())
                needs.push_back("mqttPassword");
            if (group == Group::Alerts && site.hasNtfy && site.ntfyTokenSet && config.alerts.ntfyToken.empty())
                needs.push_back("ntfyToken");
            return needs;
        }

        void writeSyncStatus(JsonObject out, const SyncView &view)
        {
            const Config &config = view.config;
            const ConfigRead &read = view.read;
            const bool addressSet = view.addressSet;
            const uint64_t nowMs = view.nowMs;
            bool anyFollows = false;
            for (Group group : GROUPS)
                anyFollows = anyFollows || groupOf(config.sqmeterSync, group).follow;
            const char *problem = !anyFollows ? nullptr : !addressSet ? problemName(Problem::NoAddress) : problemName(read.problem);
            if (problem != nullptr)
                out["problem"] = problem;
            else
                out["problem"] = nullptr;
            if (read.goodAtMs != 0 && nowMs >= read.goodAtMs)
                out["readAgeSeconds"] = static_cast<uint32_t>((nowMs - read.goodAtMs) / 1000);
            else
                out["readAgeSeconds"] = nullptr;
            for (Group group : GROUPS)
            {
                const SqmeterSyncConfig::Group &g = groupOf(config.sqmeterSync, group);
                JsonObject item = out.createNestedObject(groupName(group));
                item["follow"] = g.follow;
                if (g.syncedAt != 0)
                    item["syncedAt"] = g.syncedAt;
                else
                    item["syncedAt"] = nullptr;
                const std::string &refused = view.state.refused[static_cast<size_t>(group)];
                if (g.follow && !refused.empty())
                    item["refused"] = refused;
                else
                    item["refused"] = nullptr;
                JsonArray needs = item.createNestedArray("needs");
                if (read.goodAtMs != 0)
                    for (const char *secret : secretsNeeded(config, read.site, group))
                        needs.add(secret);
            }
        }
    } // namespace Site
} // namespace SQM
