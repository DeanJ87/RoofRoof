#include "InputsReport.h"

namespace SQM
{
    namespace Safety
    {
        namespace
        {
            // A reply's text can be as long as the far end makes it; the
            // document stays within its capacity whatever arrives.
            constexpr size_t MAX_TEXT = 240;

            std::string clip(const std::string &text)
            {
                if (text.size() <= MAX_TEXT)
                    return text;
                size_t cut = MAX_TEXT;
                while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
                    --cut; // never split a UTF-8 character
                return text.substr(0, cut) + "...";
            }

            void writeSource(JsonObject out, const SourceStatus &s, uint32_t generation)
            {
                out["enabled"] = s.state != SourceState::Off;
                out["state"] = sourceStateName(s.state);
                out["generation"] = generation;
                out["ageMs"] = s.ageMs;
                out["limitMs"] = s.limitMs;
                out["safe"] = s.sqmeterSafe;
                out["flags"] = s.sqmeterFlags;
                out["text"] = clip(describeSource(s));
            }

            void writeNina(JsonObject out, const Nina::MountReading &n, uint32_t generation, TimeMs nowMs)
            {
                const bool good = n.status == Nina::ReadingStatus::Good;
                out["state"] = ninaStateName(n.status);
                out["generation"] = generation;
                out["connected"] = good && n.connected;
                out["atPark"] = good && n.atPark;
                out["slewing"] = good && n.slewing;
                out["ageMs"] = elapsedMs(n.takenAtMs, nowMs);
                out["text"] = good ? std::string() : clip(n.detail);
            }
        } // namespace

        void writeInputsReport(JsonObject out, const InputsReport &r)
        {
            JsonObject weather = out.createNestedObject("weather");
            weather["verdict"] = verdictName(r.weather.verdict);
            weather["text"] = clip(describe(r.weather));
            writeSource(weather.createNestedObject("http"), r.weather.http, r.httpGeneration);
            writeSource(weather.createNestedObject("mqtt"), r.weather.mqtt, r.mqttGeneration);
            weather["rainContact"] = rainContactName(r.weather.rainContact);
            JsonObject park = out.createNestedObject("park");
            park["parked"] = r.park.parked;
            park["text"] = clip(describe(r.park));
            writeNina(out.createNestedObject("nina"), r.nina, r.ninaGeneration, r.nowMs);
        }

        const char *sourceStateName(SourceState state)
        {
            switch (state)
            {
            case SourceState::Off:
                return "off";
            case SourceState::Waiting:
                return "waiting";
            case SourceState::Good:
                return "good";
            case SourceState::Stale:
                return "stale";
            case SourceState::Unreachable:
                return "unreachable";
            case SourceState::Unreadable:
                return "unreadable";
            case SourceState::Offline:
                break;
            }
            return "offline";
        }

        const char *rainContactName(RainContact contact)
        {
            switch (contact)
            {
            case RainContact::NotFitted:
                return "notFitted";
            case RainContact::Dry:
                return "dry";
            case RainContact::Raining:
                return "raining";
            case RainContact::Unreadable:
                break;
            }
            return "unreadable";
        }

        const char *ninaStateName(Nina::ReadingStatus status)
        {
            switch (status)
            {
            case Nina::ReadingStatus::NotConfigured:
                return "notConfigured";
            case Nina::ReadingStatus::Waiting:
                return "waiting";
            case Nina::ReadingStatus::Good:
                return "good";
            case Nina::ReadingStatus::Unreachable:
                return "unreachable";
            case Nina::ReadingStatus::Unreadable:
                return "unreadable";
            case Nina::ReadingStatus::ApiError:
                break;
            }
            return "apiError";
        }
    } // namespace Safety
} // namespace SQM
