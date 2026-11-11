#pragma once

// The weather verdict (constitution III; spec 004 US1): SQMeter over HTTP
// and/or MQTT plus the optional rain contact, combined into safe, unsafe (with
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
            uint32_t goodAtMs = 0; // when the last good reply arrived
            SqmeterReply last;     // the last good reply
            Failure failure = Failure::None;
            uint32_t failureAtMs = 0; // when the latest failure happened
            std::string detail;       // its text ("connection refused", "missing reasonFlags")
            Availability availability = Availability::Unknown;

            void recordGood(const SqmeterReply &reply, uint32_t nowMs);
            void recordFailure(Failure kind, const std::string &text, uint32_t nowMs);
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
            uint32_t sqmeterFlags = 0; // from the last good reply, even when no longer fresh
            std::string detail;        // the latest failure's text
        };

        // The time a source is judged at.
        struct SourceClock
        {
            uint32_t limitMs;     // stale after this long without a good reply
            uint32_t startedAtMs; // when the source was switched on (boot or settings)
            uint32_t nowMs;
        };

        SourceStatus assessSource(const SourceRecord &record, SourceKind kind, const SourceClock &clock);

        // The local rain contact as spec 003 reads it.
        enum class RainContact : uint8_t
        {
            NotFitted,
            Dry,
            Raining,
            Unreadable,
        };

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
            ReasonRainContactUnreadable = 1u << 8,
        };

        struct WeatherInputs
        {
            SourceRecord http;
            SourceRecord mqtt;
            RainContact rainContact = RainContact::NotFitted;
            uint32_t httpLimitMs = 30000; // the stale-time setting
            uint32_t mqttLimitMs = MQTT_STALE_MS;
            uint32_t startedAtMs = 0;
            uint32_t nowMs = 0;
        };

        struct WeatherVerdict
        {
            Verdict verdict = Verdict::Unknown;
            uint32_t reasons = 0;
            SourceStatus http;
            SourceStatus mqtt;
            RainContact rainContact = RainContact::NotFitted;

            bool safe() const { return verdict == Verdict::Safe; }
        };

        WeatherVerdict evaluateWeather(const WeatherInputs &inputs);

        // One line about a source that isn't fine ("SQMeter HTTP stale: no good
        // reply for 75 s > 30 s"); "" for Off or good-and-safe.
        std::string describeSource(const SourceStatus &status);
        // "safe", "unknown: ...", "unsafe: ...; ..." - every problem, with the
        // measured value and the limit or the input that failed (constitution III).
        std::string describe(const WeatherVerdict &verdict);
        const char *verdictName(Verdict verdict);
        // "30 s", "30.5 s".
        std::string formatSeconds(uint32_t ms);
        // `now - then`, or 0 for a timestamp after `now`; safe across the
        // millis() wrap.
        uint32_t elapsedMs(uint32_t thenMs, uint32_t nowMs);
    } // namespace Safety
} // namespace SQM
