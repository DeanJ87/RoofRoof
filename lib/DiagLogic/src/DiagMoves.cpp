#include "DiagDocuments.h"

#include <algorithm>
#include <cstdio>

// The summary list, one move and its CSV, in parts (contracts/diagnostics.md).

namespace SQM
{
    namespace Diag
    {
        namespace
        {
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

            void writeTimes(JsonObject out, const Summary &s, const Clock &clock)
            {
                if (s.kept)
                {
                    out["startMs"] = nullptr;
                    out["endMs"] = nullptr;
                    out["startEpoch"] = s.startEpoch;
                    out["endEpoch"] = s.endEpoch;
                    return;
                }
                out["startMs"] = s.startMs;
                out["endMs"] = s.endMs;
                out["startEpoch"] = epochOf(clock, s.startMs);
                out["endEpoch"] = epochOf(clock, s.endMs);
            }

            void writeFault(JsonObject out, const Summary &s)
            {
                if (s.result != Result::Fault)
                {
                    out["fault"] = nullptr;
                    return;
                }
                JsonObject fault = out.createNestedObject("fault");
                fault["kind"] = Roof::faultName(s.fault);
                putReading(fault, "measured", s.faultMeasured, 2);
                putReading(fault, "limit", s.faultLimit, 2);
            }

            // A reading for a point row: "null" (JSON) or "" (CSV) when there is none.
            void appendValue(std::string &out, float value, bool csv)
            {
                if (!std::isfinite(value))
                {
                    out += csv ? "" : "null";
                    return;
                }
                char text[16];
                std::snprintf(text, sizeof(text), "%.2f", static_cast<double>(value));
                out += text;
            }

            void appendRow(std::string &out, const Move &move, uint8_t index, bool csv)
            {
                const Point &point = move.points[index];
                char head[16];
                std::snprintf(head, sizeof(head), "%lu,", static_cast<unsigned long>(pointStartMs(move, index)));
                out += head;
                appendValue(out, pointAmpsAvg(point), csv);
                out += ',';
                appendValue(out, pointAmpsMax(point), csv);
                out += ',';
                appendValue(out, pointVolts(point), csv);
                char duty[8];
                std::snprintf(duty, sizeof(duty), ",%d", static_cast<int>(std::lround(pointDutyPercent(point))));
                out += duty;
            }

            // Rows [from, from + POINTS_PER_PART) of the profile.
            bool pointsPart(const Move &move, size_t part, bool csv, std::string &out)
            {
                const size_t from = part * POINTS_PER_PART;
                if (from >= move.count)
                    return false;
                const size_t to = std::min<size_t>(move.count, from + POINTS_PER_PART);
                for (size_t i = from; i < to; ++i)
                {
                    if (csv)
                    {
                        appendRow(out, move, static_cast<uint8_t>(i), true);
                        out += "\r\n";
                        continue;
                    }
                    if (i > 0)
                        out += ',';
                    out += '[';
                    appendRow(out, move, static_cast<uint8_t>(i), false);
                    out += ']';
                }
                return true;
            }
        } // namespace

        void writeSummary(JsonObject out, const Summary &s, const Clock &clock, bool held)
        {
            out["id"] = s.id;
            out["active"] = s.active;
            out["kept"] = s.kept;
            out["profile"] = held && !s.kept;
            writeTimes(out, s, clock);
            out["origin"] = originName(s.origin);
            out["target"] = Roof::endName(s.target);
            out["endState"] = Roof::stateName(s.endState);
            if (s.active)
                out["result"] = nullptr;
            else
                out["result"] = resultName(s.result);
            writeFault(out, s);
            out["durationMs"] = s.durationMs();
            out["drivenMs"] = s.drivenMs;
            out["fromUnknown"] = s.fromUnknown;
            putReading(out, "peakAmps", s.peakAmps, 2);
            putReading(out, "averageAmps", s.averageAmps, 2);
            putReading(out, "stallAmps", s.stallAmps, 2);
            putReading(out, "restVolts", s.restVolts, 2);
            putReading(out, "minVolts", s.minVolts, 2);
            putReading(out, "sagVolts", s.sagVolts(), 2);
            out["hallStart"] = s.hallStart;
            out["hallEnd"] = s.hallEnd;
        }

        std::string summaryJson(const Summary &summary, const Clock &clock, bool held)
        {
            DynamicJsonDocument doc(SUMMARY_DOC_CAPACITY); // on the heap: callers run on small task stacks
            writeSummary(doc.to<JsonObject>(), summary, clock, held);
            std::string text;
            serializeJson(doc, text);
            return text;
        }

        bool listPart(const SummaryList &list, const ListView &view, size_t index, std::string &out)
        {
            out.clear();
            if (index == 0)
            {
                char head[128];
                std::snprintf(
                    head,
                    sizeof(head),
                    "{\"enabled\":true,\"keep\":%s,\"maxMoves\":%u,\"maxSummaries\":%u,\"moves\":[",
                    view.keep ? "true" : "false",
                    static_cast<unsigned>(MAX_MOVES),
                    static_cast<unsigned>(MAX_SUMMARIES));
                out = head;
                return true;
            }
            if (index <= list.size())
            {
                const Summary &s = list.at(static_cast<uint8_t>(index - 1));
                out = (index > 1 ? "," : "") + summaryJson(s, view.clock, view.held.holds(s.id));
                return true;
            }
            if (index == list.size() + 1u)
            {
                out = "]}";
                return true;
            }
            return false;
        }

        bool movePart(const Move &move, const Clock &clock, size_t index, std::string &out)
        {
            out.clear();
            if (index == 0)
            {
                char tail[160];
                std::snprintf(
                    tail,
                    sizeof(tail),
                    ",\"intervalMs\":%lu,\"columns\":[\"tMs\",\"ampsAvg\",\"ampsMax\",\"voltsMin\",\"dutyPercent\"],\"points\":[",
                    static_cast<unsigned long>(move.intervalMs));
                out = "{\"enabled\":true,\"move\":" + summaryJson(move.summary, clock, true) + tail;
                return true;
            }
            if (pointsPart(move, index - 1, false, out))
                return true;
            const size_t parts = (move.count + POINTS_PER_PART - 1u) / POINTS_PER_PART;
            if (index == parts + 1)
            {
                out = "]}";
                return true;
            }
            return false;
        }

        bool csvPart(const Move &move, size_t index, std::string &out)
        {
            out.clear();
            if (index == 0)
            {
                out = "t_ms,amps_avg,amps_max,volts_min,duty_percent\r\n";
                return true;
            }
            return pointsPart(move, index - 1, true, out);
        }
    } // namespace Diag
} // namespace SQM
