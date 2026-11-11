#include "SafetyInput.h"

#include <cstdio>

namespace SQM
{
    namespace Safety
    {
        namespace
        {
            constexpr uint32_t RAIN_BITS = SQMETER_RAIN | SQMETER_RAIN_SENSOR_FAULT;

            uint32_t saturatingAdd(uint32_t a, uint32_t b)
            {
                return a > UINT32_MAX - b ? UINT32_MAX : a + b;
            }

            // A failure newer than the last good reply (or with none at all).
            bool failingNow(const SourceRecord &record)
            {
                if (record.failure == Failure::None)
                    return false;
                return !record.haveGood || static_cast<int32_t>(record.failureAtMs - record.goodAtMs) >= 0;
            }

            SourceState failedState(const SourceRecord &record)
            {
                if (!failingNow(record))
                    return SourceState::Stale;
                return record.failure == Failure::Unreachable ? SourceState::Unreachable : SourceState::Unreadable;
            }

            const char *sourceName(SourceKind kind)
            {
                return kind == SourceKind::SqmeterHttp ? "SQMeter HTTP" : "SQMeter MQTT";
            }

            uint32_t reasonFor(SourceState state)
            {
                switch (state)
                {
                case SourceState::Stale:
                    return ReasonSourceStale;
                case SourceState::Unreachable:
                    return ReasonSourceUnreachable;
                case SourceState::Unreadable:
                    return ReasonSourceUnreadable;
                case SourceState::Offline:
                    return ReasonSourceOffline;
                case SourceState::Off:
                case SourceState::Waiting:
                case SourceState::Good:
                    break;
                }
                return 0;
            }

            // The reasons one SQMeter source contributes.
            uint32_t sourceReasons(const SourceStatus &s)
            {
                if (s.state == SourceState::Off)
                    return 0;
                uint32_t reasons = reasonFor(s.state);
                // Rain is acted on even when its source has since gone quiet (constitution III).
                if (s.sqmeterFlags & SQMETER_RAIN)
                    reasons |= ReasonRain;
                if (s.sqmeterFlags & SQMETER_RAIN_SENSOR_FAULT)
                    reasons |= ReasonRainSensorFault;
                // Unsafe for something other than rain: other bits, or `safe` false with no bits.
                const bool otherUnsafe = (s.sqmeterFlags & ~RAIN_BITS) != 0 || (s.sqmeterFlags == 0 && !s.sqmeterSafe);
                if (s.state == SourceState::Good && otherUnsafe)
                    reasons |= ReasonWeatherUnsafe;
                return reasons;
            }

            uint32_t rainContactReasons(RainContact contact)
            {
                if (contact == RainContact::Raining)
                    return ReasonRain;
                if (contact == RainContact::Unreadable)
                    return ReasonRainContactUnreadable;
                return 0;
            }

            std::string hex(uint32_t value)
            {
                char text[16];
                std::snprintf(text, sizeof(text), "0x%X", static_cast<unsigned>(value));
                return text;
            }

            std::string flagsText(uint32_t flags)
            {
                std::string text = "reasonFlags " + hex(flags);
                if (flags & SQMETER_RAIN)
                    text += " (rain)";
                if (flags & SQMETER_RAIN_SENSOR_FAULT)
                    text += " (rain sensor fault)";
                return text;
            }

            std::string ageText(const SourceStatus &s)
            {
                return "no good reply for " + formatSeconds(s.ageMs) + " > " + formatSeconds(s.limitMs);
            }

            std::string failureText(const SourceStatus &s, const char *what)
            {
                const std::string detail = s.detail.empty() ? "" : ": " + s.detail;
                return std::string(sourceName(s.kind)) + " " + what + detail + " (" + ageText(s) + ")";
            }

            void append(std::string &text, const std::string &line)
            {
                if (line.empty())
                    return;
                text += text.empty() ? "" : "; ";
                text += line;
            }
        } // namespace

        void SourceRecord::recordGood(const SqmeterReply &reply, uint32_t nowMs)
        {
            haveGood = true;
            goodAtMs = nowMs;
            last = reply;
        }

        void SourceRecord::recordFailure(Failure kind, const std::string &text, uint32_t nowMs)
        {
            failure = kind;
            failureAtMs = nowMs;
            detail = text;
        }

        uint32_t elapsedMs(uint32_t thenMs, uint32_t nowMs)
        {
            const uint32_t elapsed = nowMs - thenMs;
            return static_cast<int32_t>(elapsed) < 0 ? 0 : elapsed;
        }

        SourceStatus assessSource(const SourceRecord &record, SourceKind kind, const SourceClock &clock)
        {
            SourceStatus s;
            s.kind = kind;
            s.limitMs = clock.limitMs;
            s.detail = failingNow(record) ? record.detail : std::string();
            if (!record.enabled)
                return s;
            if (record.haveGood)
            {
                s.sqmeterSafe = record.last.safe;
                s.sqmeterFlags = record.last.reasonFlags;
                s.ageMs = saturatingAdd(elapsedMs(record.goodAtMs, clock.nowMs), record.last.evaluatedAgeMs);
            }
            else
            {
                s.ageMs = elapsedMs(clock.startedAtMs, clock.nowMs);
            }
            const bool trusted = kind == SourceKind::SqmeterHttp || record.availability == Availability::Online;
            if (kind == SourceKind::SqmeterMqtt && record.availability == Availability::Offline)
                s.state = SourceState::Offline;
            else if (record.haveGood && trusted && s.ageMs <= clock.limitMs)
                s.state = SourceState::Good;
            else if (elapsedMs(clock.startedAtMs, clock.nowMs) <= clock.limitMs)
                s.state = SourceState::Waiting;
            else
                s.state = failedState(record);
            return s;
        }

        WeatherVerdict evaluateWeather(const WeatherInputs &in)
        {
            WeatherVerdict v;
            v.http = assessSource(in.http, SourceKind::SqmeterHttp, SourceClock{in.httpLimitMs, in.startedAtMs, in.nowMs});
            v.mqtt = assessSource(in.mqtt, SourceKind::SqmeterMqtt, SourceClock{in.mqttLimitMs, in.startedAtMs, in.nowMs});
            v.rainContact = in.rainContact;

            v.reasons = sourceReasons(v.http) | sourceReasons(v.mqtt) | rainContactReasons(in.rainContact);
            if (!in.http.enabled && !in.mqtt.enabled)
                v.reasons |= ReasonNoWeatherSource;

            if (v.reasons != 0)
                v.verdict = Verdict::Unsafe;
            else if (v.http.state == SourceState::Waiting || v.mqtt.state == SourceState::Waiting)
                v.verdict = Verdict::Unknown;
            else
                v.verdict = Verdict::Safe;
            return v;
        }

        std::string describeSource(const SourceStatus &s)
        {
            const std::string name = sourceName(s.kind);
            switch (s.state)
            {
            case SourceState::Off:
                return "";
            case SourceState::Waiting:
                return name + " waiting for a first reply";
            case SourceState::Good:
                if (s.sqmeterSafe && s.sqmeterFlags == 0)
                    return "";
                return name + " says unsafe: " + flagsText(s.sqmeterFlags);
            case SourceState::Stale:
                return name + " stale: " + ageText(s);
            case SourceState::Unreachable:
                return failureText(s, "unreachable");
            case SourceState::Unreadable:
                return failureText(s, "unreadable");
            case SourceState::Offline:
                return name + " offline";
            }
            return "";
        }

        std::string describe(const WeatherVerdict &v)
        {
            std::string problems;
            append(problems, describeSource(v.http));
            append(problems, describeSource(v.mqtt));
            if (v.rainContact == RainContact::Raining)
                append(problems, "Rain contact wet");
            if (v.rainContact == RainContact::Unreadable)
                append(problems, "Rain contact unreadable");
            if (v.reasons & ReasonNoWeatherSource)
                append(problems, "No weather source: SQMeter HTTP and MQTT are both off");
            if (v.verdict == Verdict::Safe)
                return "safe";
            return std::string(verdictName(v.verdict)) + ": " + problems;
        }

        const char *verdictName(Verdict verdict)
        {
            switch (verdict)
            {
            case Verdict::Safe:
                return "safe";
            case Verdict::Unsafe:
                return "unsafe";
            case Verdict::Unknown:
                return "unknown";
            }
            return "unknown";
        }

        std::string formatSeconds(uint32_t ms)
        {
            char text[24];
            if (ms % 1000 == 0)
                std::snprintf(text, sizeof(text), "%u s", static_cast<unsigned>(ms / 1000));
            else
                std::snprintf(text, sizeof(text), "%u.%u s", static_cast<unsigned>(ms / 1000), static_cast<unsigned>(ms % 1000 / 100));
            return text;
        }
    } // namespace Safety
} // namespace SQM
