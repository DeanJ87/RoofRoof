#include "RoofStatus.h"

#include "MoveGuard.h"
#include "RoofText.h"

// Spec 027: the status's park wait and overrides (contracts/api.md).

namespace SQM
{
    namespace RoofIo
    {
        namespace
        {
            void writeBits(JsonArray out, uint8_t bits)
            {
                for (const uint8_t bit : {OverridePark, OverrideWeather, OverrideDaylight})
                    if ((bits & bit) != 0)
                        out.add(overrideName(bit));
            }

            const char *originWord(Roof::Origin origin)
            {
                switch (origin)
                {
                case Roof::Origin::Client:
                    return "alpaca";
                case Roof::Origin::Mqtt:
                    return "mqtt";
                case Roof::Origin::Automatic:
                    return "automatic";
                default:
                    return "person";
                }
            }
        } // namespace

        WaitView waitView(const ParkWait &wait, uint32_t nowMs)
        {
            WaitView v;
            v.waiting = wait.waiting();
            v.command = wait.command();
            v.origin = wait.origin();
            v.leftMs = wait.leftMs(nowMs);
            v.asked = wait.asked();
            v.failed = wait.failed();
            v.text = wait.endText();
            return v;
        }

        SourcesHealth healthOf(const Safety::InputsReport &r, bool ninaSet)
        {
            SourcesHealth h;
            const Nina::MountReading &n = r.nina;
            h.ninaSet = ninaSet;
            h.ninaState = Safety::ninaStateName(n.status);
            h.ninaReachable = n.status == Nina::ReadingStatus::Good || n.status == Nina::ReadingStatus::ApiError;
            h.ninaConnected = n.status == Nina::ReadingStatus::Good && n.connected;
            h.ninaAtPark = h.ninaConnected && n.atPark;
            h.ninaAgeMs = Safety::elapsedMs(n.takenAtMs, r.nowMs);
            h.ninaText = n.status == Nina::ReadingStatus::Good ? std::string() : n.detail;
            const Safety::SourceStatus &src = r.weather.http.state != Safety::SourceState::Off ? r.weather.http : r.weather.mqtt;
            h.sqmeterOn = src.state != Safety::SourceState::Off;
            h.sqmeterState = Safety::sourceStateName(src.state);
            h.sqmeterReplied = h.sqmeterOn && src.replied;
            h.sqmeterAgeMs = src.ageMs;
            h.sqmeterText = Safety::describeSource(src);
            return h;
        }

        namespace
        {
            std::string capped(const std::string &text, size_t most)
            {
                if (text.size() <= most)
                    return text;
                size_t cut = most;
                while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
                    --cut; // never split a UTF-8 character
                return text.substr(0, cut) + "...";
            }
        } // namespace

        void writeHealth(JsonObject out, const Snapshot &s, bool texts)
        {
            const SourcesHealth &h = s.health;
            JsonObject nina = out.createNestedObject("nina");
            nina["set"] = h.ninaSet;
            nina["state"] = h.ninaState;
            nina["reachable"] = h.ninaSet && h.ninaReachable;
            nina["connected"] = h.ninaConnected;
            nina["atPark"] = h.ninaAtPark;
            nina["ageSeconds"] = h.ninaAgeMs / 1000;
            if (texts)
                nina["text"] = capped(h.ninaText, MAX_CHECK_TEXT);
            JsonObject sqm = out.createNestedObject("sqmeter");
            sqm["on"] = h.sqmeterOn;
            sqm["state"] = h.sqmeterState;
            if (h.sqmeterReplied)
                sqm["lastReplySeconds"] = h.sqmeterAgeMs / 1000;
            else
                sqm["lastReplySeconds"] = nullptr;
            if (texts)
                sqm["text"] = capped(h.sqmeterText, MAX_CHECK_TEXT);
        }

        std::string blockedText(const Snapshot &s)
        {
            if (s.status.fault.kind != Roof::FaultKind::None)
                return "Fault: " + describeFault(s.fault);
            if (!s.pinSetupText.empty())
                return s.pinSetupText;
            if (s.parkWait.waiting)
                return std::string("Waiting for the mount to park before the ") + commandWord(s.parkWait.command);
            const bool open = s.status.state == Roof::State::Open || s.status.state == Roof::State::Opening;
            if (!open && s.weather != Safety::Verdict::Safe)
                return "Open: weather not safe: " + s.weatherText;
            if (!open && s.daylight.closes)
                return "Open: daylight: " + s.daylight.text;
            if (!s.parkConfirmed)
                return std::string(open ? "Close" : "Open") + ": " + s.parkText;
            if (!open && s.stuckSwitch != 0)
                return "Open: park switch check";
            return "";
        }

        void writeDashboard(JsonObject out, const Snapshot &s)
        {
            if (s.closeRearmSeconds >= 0)
                out["closeRearmSeconds"] = s.closeRearmSeconds;
            else
                out["closeRearmSeconds"] = nullptr;
            out["daylightState"] = s.daylight.state;
            out["blocked"] = capped(blockedText(s), 120);
        }

        void writeMoveGuard(JsonObject out, const Snapshot &s)
        {
            const WaitView &w = s.parkWait;
            if (w.waiting)
            {
                JsonObject wait = out.createNestedObject("parkWait");
                wait["command"] = w.command == Roof::Command::Open ? "open" : "close";
                wait["origin"] = originWord(w.origin);
                wait["leftSeconds"] = (w.leftMs + 999) / 1000;
                wait["asked"] = w.asked;
            }
            else
                out["parkWait"] = nullptr;
            if (w.failed)
                out["parkWaitEnded"] = w.text;
            else
                out["parkWaitEnded"] = nullptr;
            writeBits(out.createNestedArray("override"), s.overrideBits);
            writeBits(out.createNestedArray("openOverridable"), s.openOverridable);
        }
    } // namespace RoofIo
} // namespace SQM
