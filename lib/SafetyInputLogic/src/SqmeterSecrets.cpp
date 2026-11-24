#include "SqmeterSecrets.h"

#include <cstring>

namespace SQM
{
    namespace Site
    {
        namespace
        {
            std::string text(JsonVariantConst value)
            {
                return value.is<const char *>() ? std::string(value.as<const char *>()) : std::string();
            }

            SecretsRead failed(SecretsProblem problem, const std::string &detail, uint64_t atMs)
            {
                SecretsRead read;
                read.problem = problem;
                read.detail = detail;
                read.atMs = atMs;
                return read;
            }

            // A 401, or a 403 that isn't one of SQMeter's two sharing refusals.
            SecretsProblem refusal(int status, const std::string &error, bool credentials)
            {
                if (status == 403 && error.find("sharing is off") != std::string::npos)
                    return SecretsProblem::SharingOff;
                if (status == 403 && error.find("web password") != std::string::npos)
                    return SecretsProblem::NoWebPassword;
                return credentials ? SecretsProblem::LoginRefused : SecretsProblem::LoginNeeded;
            }

            SecretState stateOf(const std::string &theirs, const std::string &ours)
            {
                if (theirs.empty())
                    return SecretState::None;
                return theirs == ours ? SecretState::Same : SecretState::Received;
            }

            const char *stateName(SecretState state)
            {
                return state == SecretState::Received ? "received" : state == SecretState::Same ? "same" : "none";
            }
        } // namespace

        const char *secretsProblemName(SecretsProblem problem)
        {
            static const char *const NAMES[] = {
                "received",
                "waiting",
                "notSaved",
                "notSupported",
                "sharingOff",
                "noWebPassword",
                "loginNeeded",
                "loginRefused",
                "tooManyLogins",
                "unreachable",
                "unreadable"};
            const size_t i = static_cast<size_t>(problem);
            return i < sizeof(NAMES) / sizeof(NAMES[0]) ? NAMES[i] : "unreadable";
        }

        void secretsFilter(JsonDocument &filter)
        {
            filter["error"] = true;
            filter["mqtt"]["password"] = true;
            filter["alerts"]["ntfy"]["token"] = true;
            filter["alerts"]["pushover"]["userKey"] = true;
            filter["alerts"]["pushover"]["appToken"] = true;
        }

        SecretsRead readSecrets(const Fetch &fetch, const JsonDocument &doc, uint64_t atMs)
        {
            if (fetch.status <= 0)
                return failed(SecretsProblem::Unreachable, fetch.error.empty() ? "no reply" : fetch.error, atMs);
            if (fetch.status == 404)
                return failed(SecretsProblem::NotSupported, "", atMs);
            if (fetch.status == 429)
                return failed(SecretsProblem::TooManyLogins, "", atMs);
            if (fetch.status == 401 || fetch.status == 403)
                return failed(refusal(fetch.status, text(doc["error"]), fetch.credentials), "", atMs);
            if (fetch.status != 200)
                return failed(SecretsProblem::Unreadable, "", atMs);
            if (!fetch.parsed || !doc.is<JsonObjectConst>() || !(doc["mqtt"].is<JsonObjectConst>() || doc["alerts"].is<JsonObjectConst>()))
                return failed(SecretsProblem::Unreadable, "", atMs);
            SecretsRead read;
            read.problem = SecretsProblem::None;
            read.atMs = atMs;
            read.secrets.received = true;
            read.secrets.mqttPassword = text(doc["mqtt"]["password"]);
            read.secrets.ntfyToken = text(doc["alerts"]["ntfy"]["token"]);
            read.secrets.pushoverUserKey = text(doc["alerts"]["pushover"]["userKey"]);
            read.secrets.pushoverAppToken = text(doc["alerts"]["pushover"]["appToken"]);
            return read;
        }

        bool savedAddress(const std::string &address, const SafetyConfig &saved)
        {
            const std::string origin = siteOrigin(address);
            return !origin.empty() && origin == siteOrigin(saved.httpUrl);
        }

        CopiedSecrets copiedFrom(const SiteConfig &site)
        {
            CopiedSecrets held;
            held.mqttBroker = site.broker;
            held.mqttPort = site.port;
            held.mqttUsername = site.username;
            held.mqttPassword = site.secrets.mqttPassword;
            held.ntfyServer = site.ntfyServer;
            held.ntfyToken = site.secrets.ntfyToken;
            held.pushoverUserKey = site.secrets.pushoverUserKey;
            held.pushoverAppToken = site.secrets.pushoverAppToken;
            return held;
        }

        SecretStates secretStates(const SiteSecrets &secrets, const Config &saved)
        {
            SecretStates states;
            if (!secrets.received)
                return states;
            const AlertsConfig &a = saved.alerts;
            states.mqttPassword = stateOf(secrets.mqttPassword, saved.mqtt.password);
            states.ntfyToken = stateOf(secrets.ntfyToken, a.ntfyToken);
            // Pushover needs both keys: none unless SQMeter has both.
            const bool both = !secrets.pushoverUserKey.empty() && !secrets.pushoverAppToken.empty();
            const bool same = secrets.pushoverUserKey == a.pushoverUserKey && secrets.pushoverAppToken == a.pushoverAppToken;
            states.pushover = !both ? SecretState::None : same ? SecretState::Same : SecretState::Received;
            return states;
        }

        void writeSecretsPart(JsonObject site, const SecretsRead &read, const SecretStates &states, const std::string &address)
        {
            JsonObject out = site.createNestedObject("secrets");
            if (read.good())
                out["problem"] = nullptr;
            else
                out["problem"] = secretsProblemName(read.problem);
            out["http"] = siteOrigin(address).rfind("http://", 0) == 0;
            out["mqttPassword"] = stateName(states.mqttPassword);
            out["ntfyToken"] = stateName(states.ntfyToken);
            out["pushover"] = stateName(states.pushover);
        }

        bool sharingOff(const SiteConfig &site)
        {
            return site.hasSharing && !site.sharingSecrets;
        }

        SecretsRead sharingOffRead(uint64_t atMs)
        {
            return failed(SecretsProblem::SharingOff, "", atMs);
        }

        bool secretsDue(const LocationSchedule::Pass &pass, uint64_t settingsGoodAtMs, const SecretsRead &last)
        {
            const bool loginFailed = last.problem == SecretsProblem::LoginNeeded || last.problem == SecretsProblem::LoginRefused;
            const bool safetySoon = pass.safetyNextMs != 0 && pass.nowMs + LocationSchedule::READ_MS > pass.safetyNextMs;
            return pass.enabled && !pass.busy && !pass.safetyPolled && !safetySoon && !loginFailed && settingsGoodAtMs != 0 &&
                   settingsGoodAtMs != last.basedOnMs;
        }

        void pairSecrets(SiteConfig &site, uint64_t settingsGoodAtMs, const SecretsRead &read)
        {
            if (read.good() && read.basedOnMs == settingsGoodAtMs && settingsGoodAtMs != 0)
                site.secrets = read.secrets;
            else
                site.secrets = SiteSecrets{};
        }
    } // namespace Site
} // namespace SQM
