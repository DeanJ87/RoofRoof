#pragma once

// The weather verdict (constitution III; spec 004 US1): SQMeter over HTTP
// and/or MQTT (spec 023 removed the local rain contact), combined into safe, unsafe (with
// reasons) or unknown. Doubt is unsafe: a source that is stale, unreachable,
// unreadable, offline or lost makes the verdict unsafe. Unknown only covers
// the start-up window, and is never safe. Rules: specs/004-safety-inputs/data-model.md.

#include <cstdint>
#include <string>

#include "SqmeterReply.h"

namespace SQM
{
    namespace Safety
    {
        // A time in milliseconds since start, 64-bit so it never wraps (a
        // 32-bit millis() wraps after 49.7 days and would make an old reply look new).
        using TimeMs = uint64_t;

        // SQMeter's MQTT safe topic is republished every 60 s; two missed
        // refreshes plus margin (research D3).
        constexpr uint32_t MQTT_STALE_MS = 150000;

        enum class SourceKind : uint8_t
        {
            SqmeterHttp,
            SqmeterMqtt,
        };

        // Why the latest attempt gave nothing usable.
        enum class Failure : uint8_t
        {
            None,
            Unreachable, // no connection, timeout
            Unreadable,  // a reply that isn't the documented shape (wrong status, payload or JSON)
        };

        // SQMeter's MQTT availability (its last will). HTTP leaves it Unknown.
        enum class Availability : uint8_t
        {
            Unknown,
            Online,
            Offline,
        };

        // What the firmware has seen from one SQMeter source.
        struct SourceRecord
        {
            bool enabled = false;
            bool haveGood = false;
            TimeMs goodAtMs = 0; // when the last good reply arrived
            SqmeterReply last;   // the last good reply
            Failure failure = Failure::None;
            TimeMs failureAtMs = 0; // when the latest failure happened
            std::string detail;     // its text ("connection refused", "missing reasonFlags")
            Availability availability = Availability::Unknown;
            // MQTT: whether a message has arrived on <base>/safe and <base>/safety
            // since the last subscription. The first one is usually the broker's
            // retained copy, which may be old (see applyMqttMessage).
            bool safeSeen = false;
            bool safetySeen = false;

            void recordGood(const SqmeterReply &reply, TimeMs nowMs);
            void recordFailure(Failure kind, const std::string &text, TimeMs nowMs);
            // The topics were (re)subscribed: the broker replays retained copies next.
            void resubscribed();
        };

        // A source as judged at `now`.
        enum class SourceState : uint8_t
        {
            Off,
            Waiting, // start-up window, no good reply yet
            Good,    // fresh; `sqmeterSafe` and `sqmeterFlags` say what it reported
            Stale,
            Unreachable,
            Unreadable,
            Offline,
        };

        struct SourceStatus
        {
            SourceKind kind = SourceKind::SqmeterHttp;
            SourceState state = SourceState::Off;
            uint32_t ageMs = 0;   // since the last good reply (or since start, without one)
            uint32_t limitMs = 0; // the stale limit
            bool sqmeterSafe = false;
            uint32_t sqmeterFlags = 0;  // from the last good reply, even when no longer fresh
            std::string sqmeterReasons; // spec 030: that reply's reasons as words ("" without them)
            std::string detail;         // the latest failure's text
            bool replied = false;       // spec 027: a good reply since the source was switched on (ageMs counts from it)
        };

        // The time a source is judged at.
        struct SourceClock
        {
            uint32_t limitMs;   // stale after this long without a good reply
            TimeMs startedAtMs; // when the source was switched on (boot or settings)
            TimeMs nowMs;
        };

        SourceStatus assessSource(const SourceRecord &record, SourceKind kind, const SourceClock &clock);

        enum class Verdict : uint8_t
        {
            Safe,
            Unsafe,
            Unknown,
        };

        // Why the verdict isn't safe (bits of WeatherVerdict::reasons).
        enum WeatherReason : uint32_t
        {
            ReasonRain = 1u << 0,
            ReasonRainSensorFault = 1u << 1,
            ReasonWeatherUnsafe = 1u << 2, // SQMeter unsafe for anything but rain
            ReasonSourceStale = 1u << 3,
            ReasonSourceUnreachable = 1u << 4,
            ReasonSourceUnreadable = 1u << 5,
            ReasonSourceOffline = 1u << 6,
            ReasonNoWeatherSource = 1u << 7,
            // 1u << 8 was the rain contact (removed by spec 023); left unused so
            // the other bits keep their values.
        };

        struct WeatherInputs
        {
            SourceRecord http;
            SourceRecord mqtt;
            uint32_t httpLimitMs = 30000; // the stale-time setting
            uint32_t mqttLimitMs = MQTT_STALE_MS;
            TimeMs startedAtMs = 0;
            TimeMs nowMs = 0;
        };

        struct WeatherVerdict
        {
            Verdict verdict = Verdict::Unknown;
            uint32_t reasons = 0;
            SourceStatus http;
            SourceStatus mqtt;
            // Spec 022: safe only because the test box's weather bypass stands in
            // for a missing weather source (TestBox::applyWeather).
            bool bypassed = false;

            bool safe() const { return verdict == Verdict::Safe; }
        };

        WeatherVerdict evaluateWeather(const WeatherInputs &inputs);

        // One line about a source that isn't fine ("SQMeter HTTP stale: no good
        // reply for 75 s > 30 s"); "" for Off or good-and-safe.
        std::string describeSource(const SourceStatus &status);
        // "safe", "unknown: ...", "unsafe: ...; ..." - every problem, with the
        // measured value and the limit or the input that failed (constitution III).
        std::string describe(const WeatherVerdict &verdict);
        // "cloud cover, rain": SQMeter's unsafe flags as words, for people
        // (spec 030; an unknown bit reads "other reason (bit N)", none "no reason given").
        std::string reasonFlagsText(uint32_t flags);
        // Spec 030: SQMeter's own reasons text, or the flags as words without it.
        std::string unsafeText(uint32_t flags, const std::string &reasons);
        const char *verdictName(Verdict verdict);
        // "30 s", "30.5 s".
        std::string formatSeconds(uint32_t ms);
        // `now - then`, 0 for a timestamp after `now` (taken after `now` was
        // sampled), saturating at UINT32_MAX (49 days, far past any limit).
        uint32_t elapsedMs(TimeMs thenMs, TimeMs nowMs);
    } // namespace Safety
} // namespace SQM
