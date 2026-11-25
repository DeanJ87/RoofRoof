#pragma once

// What one SQMeter answer does to its source's record (spec 004 FR-010,
// FR-011): the HTTP poll's outcome, and each MQTT message. The firmware
// (src/SafetyPoller.cpp, src/SafetyInputs.cpp) only fetches and hands over.

#include <string>

#include "SafetyInput.h"
#include "SqmeterReply.h"
#include <ArduinoJson.h>

namespace SQM
{
    namespace Safety
    {
        // One HTTP GET of /api/safety: the status (> 0) and body, or why
        // there was no reply.
        struct HttpOutcome
        {
            int status = 0;
            std::string body;
            std::string error;
            bool credentials = false; // spec 020: SQMeter's login was sent
        };

        // Spec 020: what a 401 or 403 from SQMeter means, for people: "SQMeter
        // wants a password" (none was sent) or "SQMeter refused the password";
        // nullptr for any other status.
        const char *loginRefusalText(int status, bool credentialsSent);

        // No reply: unreachable. Not 200, or not the documented document:
        // unreadable, naming why ("that address answered HTTP 404, not like
        // SQMeter's /api/safety"; spec 020 FR-037). Otherwise a good reply.
        struct HttpCheck
        {
            Failure failure = Failure::None;
            std::string detail;
            SqmeterReply reply;
        };
        HttpCheck checkHttpReply(const HttpOutcome &outcome);
        void recordHttpReply(SourceRecord &record, const HttpOutcome &outcome, TimeMs nowMs);

        // Spec 020 FR-038: one fetch for Settings' Test, as POST
        // /api/sqmeter/test answers it: {"safe", "reasonFlags", "reasons", "text"} or
        // {"error"} (the reason, as a sentence).
        void writeTestResult(JsonObject out, const HttpOutcome &outcome);

        // SQMeter's topics under its base topic.
        struct MqttTopics
        {
            std::string availability;
            std::string safe;
            std::string safety;

            static MqttTopics under(const std::string &base);
        };

        // Applies one message. Topics other than these three are ignored.
        // After a (re)subscription (SourceRecord::resubscribed) the first
        // message on <base>/safe and on <base>/safety is usually the broker's
        // retained copy, possibly left by an SQMeter that is long gone: it is
        // used only if it says unsafe. A retained "safe" never counts.
        void applyMqttMessage(
            SourceRecord &record, const MqttTopics &topics, const std::string &topic, const std::string &payload, TimeMs nowMs);
    } // namespace Safety
} // namespace SQM
