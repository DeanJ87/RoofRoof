#include "RoofStatus.h"

#include <cmath>

#include "RoofSettings.h"
#include "RoofText.h"

namespace SQM
{
    namespace RoofIo
    {
        namespace
        {
            // `value` rounded to `decimals`, or null when it isn't finite.
            void putReading(JsonObject out, const char *key, float value, int decimals)
            {
                if (!std::isfinite(value))
                {
                    out[key] = nullptr;
                    return;
                }
                const double scale = std::pow(10.0, decimals);
                out[key] = std::round(static_cast<double>(value) * scale) / scale;
            }

            const char *switchName(uint8_t fitted, uint8_t index, Contact contact)
            {
                return fitted > index ? contactName(contact) : "notFitted";
            }

            const char *rainName(const Snapshot &s)
            {
                if (!s.rainFitted)
                    return "notFitted";
                switch (s.rain)
                {
                case Contact::Closed:
                    return "raining";
                case Contact::Open:
                    return "dry";
                default:
                    return "unreadable";
                }
            }

            // `text` cut to MAX_CHECK_TEXT bytes, never inside a UTF-8 character.
            std::string capped(const std::string &text)
            {
                if (text.size() <= MAX_CHECK_TEXT)
                    return text;
                size_t cut = MAX_CHECK_TEXT - 3;
                while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
                    --cut;
                return text.substr(0, cut) + "...";
            }

            void writePosition(JsonObject out, const Snapshot &s)
            {
                const Roof::Status &st = s.status;
                out["positionKnown"] = st.positionKnown;
                if (st.positionKnown && s.travelMm > 0)
                {
                    putReading(out, "positionMm", st.positionMm, 0);
                    putReading(out, "positionPercent", st.positionMm * 100.0f / static_cast<float>(s.travelMm), 1);
                }
                else
                {
                    out["positionMm"] = nullptr;
                    out["positionPercent"] = nullptr;
                }
                const bool moving = st.state == Roof::State::Opening || st.state == Roof::State::Closing;
                if (moving)
                    out["target"] = Roof::endName(st.target);
                else
                    out["target"] = nullptr;
            }

            void writeFault(JsonObject out, const Snapshot &s)
            {
                if (s.status.fault.kind == Roof::FaultKind::None)
                {
                    out["fault"] = nullptr;
                    return;
                }
                FaultRecord record = s.fault;
                record.fault = s.status.fault;
                JsonObject fault = out.createNestedObject("fault");
                fault["kind"] = Roof::faultName(record.fault.kind);
                fault["cause"] = causeName(record.cause);
                putReading(fault, "measured", record.fault.measured, 2);
                putReading(fault, "limit", record.fault.limit, 2);
                fault["text"] = describeFault(record);
            }
        } // namespace

        void applyChecks(Snapshot &snapshot, const GateInputs &gates)
        {
            snapshot.weather = gates.weather;
            snapshot.weatherText = gates.weatherText.empty() ? std::string("no verdict yet") : capped(gates.weatherText);
            snapshot.parkConfirmed = parkHolds(gates);
            snapshot.parkText = capped(snapshot.parkConfirmed ? gates.parkText : describeGate(GateRefusal::MountNotParked, gates));
        }

        bool supplyLow(const Snapshot &snapshot)
        {
            return !Roof::supplyReadable(snapshot.supplyVolts) || snapshot.supplyVolts < snapshot.minSupplyVolts;
        }

        void writeStatus(JsonObject out, const Snapshot &s)
        {
            const Roof::Status &st = s.status;
            out["state"] = Roof::stateName(st.state);
            writePosition(out, s);
            out["driving"] = st.driving;
            out["fromUnknown"] = st.fromUnknown;
            out["moveElapsedMs"] = st.moveElapsedMs;
            out["moveTimeoutMs"] = st.moveTimeoutMs;
            JsonObject hall = out.createNestedObject("hall");
            hall["level"] = Roof::hallLevelName(st.hall);
            hall["counts"] = st.hallCounts;
            putReading(out, "supplyVolts", Roof::supplyReadable(s.supplyVolts) ? s.supplyVolts : NAN, 2);
            out["supplyLow"] = supplyLow(s);
            putReading(out, "minSupplyVolts", s.minSupplyVolts, 2);
            out["currentSense"] = s.currentSense;
            putReading(out, "motorAmps", s.currentSense ? s.motorAmps : NAN, 2);
            putReading(out, "maxCurrentAmps", s.maxCurrentAmps, 2);
            JsonObject park = out.createNestedObject("parkSwitches");
            park["fitted"] = s.fittedSwitches;
            park["switch1"] = switchName(s.fittedSwitches, 0, s.switch1);
            park["switch2"] = switchName(s.fittedSwitches, 1, s.switch2);
            out["rainContact"] = rainName(s);
            out["closePending"] = s.closePending;
            out["closePhase"] = s.closePhase != nullptr ? s.closePhase : "idle";
            out["weather"] = Safety::verdictName(s.weather);
            out["weatherText"] = s.weatherText;
            out["parkConfirmed"] = s.parkConfirmed;
            out["parkText"] = s.parkText;
            writeFault(out, s);
            out["watchdogTrips"] = s.watchdogTrips;
            out["maxStepGapMs"] = s.maxStepGapMs;
            out["maxWatchdogBeatAgeMs"] = s.maxWatchdogBeatAgeMs;
            out["simulation"] = s.simulation != nullptr ? s.simulation : "off";
        }

        void writeDaylight(JsonObject out, const Snapshot &s)
        {
            JsonObject daylight = out.createNestedObject("daylight");
            daylight["mode"] = s.daylight.mode != nullptr ? s.daylight.mode : "alert";
            daylight["state"] = s.daylight.state != nullptr ? s.daylight.state : "unknown";
            putReading(daylight, "sunAltitudeDeg", s.daylight.known ? static_cast<float>(s.daylight.altitudeDeg) : NAN, 1);
            putReading(daylight, "limitDeg", s.daylight.limitDeg, 1);
            daylight["text"] = s.daylight.text;
            daylight["closes"] = s.daylight.closes;
        }
    } // namespace RoofIo
} // namespace SQM
