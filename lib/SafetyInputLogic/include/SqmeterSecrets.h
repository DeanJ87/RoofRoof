#pragma once

// Spec 024: SQMeter's own secrets (GET /api/secrets: its MQTT password, ntfy
// token and Pushover keys), read on the device with SQMeter's web login,
// for the copy and the sync. Pure: the firmware (src/WebServerSite.cpp,
// src/SafetyPoller.cpp) and the demo only fetch. No value read here is ever
// written to a document a browser gets: the copy says only which arrived.

#include <ArduinoJson.h>

#include <cstdint>
#include <string>

#include "Config.h"
#include "CopiedSecrets.h"
#include "SqmeterSite.h"

namespace SQM
{
    namespace Site
    {
        constexpr const char *SECRETS_PATH = "/api/secrets";
        constexpr size_t SECRETS_FILTER_BYTES = 384;
        constexpr size_t SECRETS_DOC_BYTES = 768;
        // How long a copy's secrets stay held for the save.
        constexpr uint32_t SECRETS_HELD_MS = 30u * 60u * 1000u;

        enum class SecretsProblem : uint8_t
        {
            None, // received
            Waiting,
            NotSaved,      // the copy's address isn't the saved SQMeter address
            NotSupported,  // 404: an SQMeter from before password sharing
            SharingOff,    // 403: SQMeter's password sharing is off
            NoWebPassword, // 403: SQMeter has no web password to guard them
            LoginNeeded,   // 401 (or another 403) with no login sent
            LoginRefused,  // 401 (or another 403) with the login sent
            TooManyLogins, // 429: SQMeter locked it after failed logins (for a minute)
            Unreachable,
            Unreadable,
        };
        // "received", "waiting", "notSaved", "notSupported", "sharingOff",
        // "noWebPassword", "loginNeeded", "loginRefused", "tooManyLogins", "unreachable", "unreadable".
        const char *secretsProblemName(SecretsProblem problem);

        struct SecretsRead
        {
            SecretsProblem problem = SecretsProblem::Waiting;
            SiteSecrets secrets;
            std::string detail;
            uint64_t atMs = 0;
            uint64_t basedOnMs = 0; // the settings read (ConfigRead::goodAtMs) it pairs with
            bool good() const { return problem == SecretsProblem::None; }
        };

        // Only the four secrets and a refusal's "error".
        void secretsFilter(JsonDocument &filter);
        // One fetch of GET /api/secrets, filtered by secretsFilter (a 403's
        // body is read too, for which refusal it is).
        SecretsRead readSecrets(const Fetch &fetch, const JsonDocument &doc, uint64_t atMs);

        // The copy reads secrets only at the saved SQMeter address (its scheme,
        // host and port).
        bool savedAddress(const std::string &address, const SafetyConfig &saved);

        // What the copy holds for the save: the secrets with SQMeter's servers.
        CopiedSecrets copiedFrom(const SiteConfig &site);

        // Each secret as the copy reports it: not received (or empty), received
        // and different from RoofRoof's saved one, or the same.
        enum class SecretState : uint8_t
        {
            None,
            Received,
            Same,
        };
        struct SecretStates
        {
            SecretState mqttPassword = SecretState::None;
            SecretState ntfyToken = SecretState::None;
            SecretState pushover = SecretState::None; // both keys
        };
        SecretStates secretStates(const SiteSecrets &secrets, const Config &saved);
        // The copy document's "secrets" part: {problem, http, mqttPassword, ntfyToken, pushover}.
        void writeSecretsPart(JsonObject site, const SecretsRead &read, const SecretStates &states, const std::string &address);

        // SQMeter's settings say its password sharing is off (`sharing.secrets`
        // false): nothing is asked for, and the read is this.
        bool sharingOff(const SiteConfig &site);
        SecretsRead sharingOffRead(uint64_t atMs);

        // The sync's secrets read is due: in a pass after a good settings read
        // it hasn't been taken for (`last`), never in a pass that polled
        // SQMeter's safety or just before that poll, never while the roof is
        // busy. Never again after a refused or missing login (401): a wrong
        // saved login must not lock SQMeter's endpoint; a changed login starts
        // a new read (the poller forgets `last`). A 429 waits for the next
        // settings read (minutes later).
        bool secretsDue(const LocationSchedule::Pass &pass, uint64_t settingsGoodAtMs, const SecretsRead &last);
        // The read's secrets on `site` when they pair with its settings read.
        void pairSecrets(SiteConfig &site, uint64_t settingsGoodAtMs, const SecretsRead &read);
    } // namespace Site
} // namespace SQM
