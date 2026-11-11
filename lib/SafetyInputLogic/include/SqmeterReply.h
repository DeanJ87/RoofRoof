#pragma once

// Reading SQMeter's safety outputs: GET /api/safety and the MQTT topics
// <base>/safe, <base>/safety and <base>/availability (SQMeter spec 013;
// specs/004-safety-inputs/contracts/sqmeter-safety.md). Anything that isn't the
// documented shape is an error, never a guess (constitution III).

#include <cstdint>
#include <optional>
#include <string>

namespace SQM
{
    namespace Safety
    {
        // SQMeter's reasonFlags bits RoofRoof acts on (SQMeter SafetyEvaluator.h).
        constexpr uint32_t SQMETER_RAIN = 1u << 9;
        constexpr uint32_t SQMETER_RAIN_SENSOR_FAULT = 1u << 10;

        // One good SQMeter answer.
        struct SqmeterReply
        {
            bool safe = false;
            uint32_t reasonFlags = 0;
            uint32_t evaluatedAgeMs = 0; // 0 when SQMeter didn't say

            // SQMeter's flags win over its `safe` (spec 004 FR-005).
            bool unsafe() const { return !safe || reasonFlags != 0; }
        };

        enum class ReplyError : uint8_t
        {
            None,
            NotJson,
            NotObject,
            MissingSafe,        // absent or not true/false
            MissingReasonFlags, // absent or not a whole number >= 0
            BadEvaluatedAge,    // present but not a whole number >= 0
        };

        // The /api/safety document (also the <base>/safety payload).
        ReplyError parseSafetyJson(const std::string &body, SqmeterReply &out);
        // "missing reasonFlags", ... for logs and reasons.
        const char *replyErrorText(ReplyError error);
        // <base>/safe: "1" -> true, "0" -> false, anything else -> nothing.
        std::optional<bool> parseSafePayload(const std::string &payload);
        // <base>/availability: "online" -> true, "offline" -> false.
        std::optional<bool> parseAvailability(const std::string &payload);
    } // namespace Safety
} // namespace SQM
