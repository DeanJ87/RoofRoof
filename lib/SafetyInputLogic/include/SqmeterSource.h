#pragma once

// What one SQMeter answer does to its source's record (spec 004 FR-010,
// FR-011): the HTTP poll's outcome, and each MQTT message. The firmware
// (src/SafetyPoller.cpp, src/SafetyInputs.cpp) only fetches and hands over.

#include <string>

#include "SafetyInput.h"

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
        };

        // No reply: unreachable. Not 200, or not the documented document:
        // unreadable, naming why. Otherwise a good reply.
        void recordHttpReply(SourceRecord &record, const HttpOutcome &outcome, TimeMs nowMs);

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
