#include "CalibrationDocument.h"

#include <cmath>

namespace SQM
{
    namespace Calib
    {
        namespace
        {
            double rounded(double value, int decimals)
            {
                const double scale = std::pow(10.0, decimals);
                return std::round(value * scale) / scale;
            }

            void writeProblem(JsonObject out, const char *key, const Problem &problem)
            {
                if (problem.reason.empty())
                {
                    out[key] = nullptr;
                    return;
                }
                JsonObject p = out.createNestedObject(key);
                p["reason"] = problem.reason;
                p["text"] = problem.text;
            }

            void writeRest(JsonObject out, const char *key, const Stats &stats, Roof::HallLevel level)
            {
                if (stats.empty())
                {
                    out[key] = nullptr;
                    return;
                }
                JsonObject r = out.createNestedObject(key);
                r["mean"] = rounded(stats.mean(), 1);
                r["min"] = stats.min;
                r["max"] = stats.max;
                r["level"] = Roof::hallLevelName(level);
            }

            void writeHall(JsonObject out, const Measurements &m)
            {
                JsonObject hall = out.createNestedObject("hall");
                if (m.between.empty())
                    hall["between"] = nullptr;
                else
                {
                    JsonObject b = hall.createNestedObject("between");
                    b["mean"] = rounded(m.between.mean(), 1);
                    b["min"] = m.between.min;
                    b["max"] = m.between.max;
                    b["noise"] = rounded(m.between.sigma(), 1);
                }
                writeRest(hall, "closed", m.closedRest, m.closedLevel);
                writeRest(hall, "open", m.openRest, m.openLevel);
            }

            void writeLegs(JsonObject out, const Measurements &m)
            {
                JsonArray legs = out.createNestedArray("legs");
                for (uint8_t i = 0; i < m.legCount; ++i)
                {
                    const MeasuredLeg &leg = m.legs[i];
                    JsonObject l = legs.createNestedObject();
                    l["name"] = legName(leg.kind);
                    l["target"] = Roof::endName(leg.target);
                    l["dutyPercent"] = leg.dutyPercent;
                    l["drivenMs"] = leg.drivenMs;
                    l["level"] = Roof::hallLevelName(leg.level);
                    l["speedMmPerSecond"] = rounded(legSpeed(leg, m.travelMm), 1);
                    const bool amps = m.currentSense && leg.electrical.count > 0;
                    if (amps)
                    {
                        l["ampsMean"] = rounded(leg.electrical.ampsMean(), 2);
                        l["ampsPeak"] = rounded(leg.electrical.ampsPeak, 2);
                    }
                    else
                    {
                        l["ampsMean"] = nullptr;
                        l["ampsPeak"] = nullptr;
                    }
                    if (leg.electrical.supplyRead)
                        l["supplyMinVolts"] = rounded(leg.electrical.supplyMin, 2);
                    else
                        l["supplyMinVolts"] = nullptr;
                }
            }

            template <typename T> void pair(JsonObject out, const char *key, bool proposed, T old, T now)
            {
                if (!proposed)
                {
                    out[key] = nullptr;
                    return;
                }
                JsonObject p = out.createNestedObject(key);
                p["old"] = old;
                p["new"] = now;
            }

            void writeProposal(JsonObject out, const Report &r)
            {
                if (!r.proposed || r.state != RunState::Done)
                {
                    out["proposal"] = nullptr;
                    return;
                }
                const Proposal &p = r.proposal;
                const Roof::Settings &o = r.old;
                JsonObject j = out.createNestedObject("proposal");
                pair<uint32_t>(j, "travelMm", true, o.travelMm, p.travelMm);
                pair<double>(j, "speedMmPerSecond", p.speed, rounded(o.speedMmPerSecond, 1), p.speedMmPerSecond);
                pair<uint32_t>(j, "timeoutMarginSeconds", p.speed, o.timeoutMarginMs / 1000, p.timeoutMarginSeconds);
                pair<const char *>(j, "hallHighEnd", p.hall, Roof::endName(o.hallHighEnd), Roof::endName(p.hallHighEnd));
                pair<uint32_t>(j, "hallHighCounts", p.hall, o.hallHighCounts, p.hallHighCounts);
                pair<uint32_t>(j, "hallLowCounts", p.hall, o.hallLowCounts, p.hallLowCounts);
                writeProblem(j, "speedProblem", p.speedProblem);
                writeProblem(j, "hallProblem", p.hallProblem);
                JsonArray warnings = j.createNestedArray("warnings");
                for (const Problem &w : p.warnings)
                {
                    JsonObject item = warnings.createNestedObject();
                    item["reason"] = w.reason;
                    item["text"] = w.text;
                }
            }

            void writeLeg(JsonObject out, const Report &r, uint32_t nowMs)
            {
                if (r.state != RunState::Running)
                {
                    out["leg"] = nullptr;
                    return;
                }
                JsonObject leg = out.createNestedObject("leg");
                leg["index"] = r.legIndex + 1;
                leg["count"] = r.legTotal;
                leg["name"] = legName(r.leg);
                leg["phase"] = phaseName(r.phase);
                leg["elapsedMs"] = nowMs - r.phaseStartMs;
                leg["limitMs"] = r.phase == Phase::Drive ? r.legLimitMs : 0;
            }
        } // namespace

        bool parseRequest(JsonVariantConst body, Request &out, const char *&error)
        {
            error = nullptr;
            if (!body.is<JsonObjectConst>())
                error = "The body must be a JSON object";
            else if (!body["attended"].is<bool>())
                error = "attended must be true or false";
            else if (!parseWhere(body["at"].as<const char *>(), out.at))
                error = "at must be closed, open or partWay";
            else if (!body["travelMm"].is<uint32_t>())
                error = "travelMm must be a whole number of mm";
            else if (!body["topPass"].isNull() && !body["topPass"].is<bool>())
                error = "topPass must be true or false";
            if (error != nullptr)
                return false;
            out.attended = body["attended"].as<bool>();
            out.travelMm = body["travelMm"].as<uint32_t>();
            out.topPass = body["topPass"] | false;
            return true;
        }

        void writeDocument(JsonObject out, const Report &r, uint32_t nowMs)
        {
            out["state"] = runStateName(r.state);
            out["active"] = r.state == RunState::Running;
            if (r.state == RunState::Idle)
                out["request"] = nullptr;
            else
            {
                JsonObject req = out.createNestedObject("request");
                req["at"] = whereName(r.request.at);
                req["travelMm"] = r.request.travelMm;
                req["topPass"] = r.request.topPass;
                req["limitSeconds"] = r.limitMs / 1000;
            }
            writeLeg(out, r, nowMs);
            if (r.ending == Ending::None)
                out["ending"] = nullptr;
            else
            {
                JsonObject ending = out.createNestedObject("ending");
                ending["reason"] = endingName(r.ending);
                ending["text"] = r.endingText;
            }
            writeLegs(out, r.measured);
            writeHall(out, r.measured);
            out["currentSense"] = r.measured.currentSense;
            out["maxCurrentAmps"] = rounded(r.measured.maxCurrentAmps, 1);
            writeProposal(out, r);
        }
    } // namespace Calib
} // namespace SQM
