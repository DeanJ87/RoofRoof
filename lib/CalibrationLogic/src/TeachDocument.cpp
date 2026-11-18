#include <cmath>
#include <cstring>

#include "Teach.h"

// Spec 019 (contracts/jog-teach-api.md): GET /api/roof/teach and the bodies of
// the jog and teach routes.

namespace SQM
{
    namespace Calib
    {
        namespace
        {
            double rounded1(double value)
            {
                return std::round(value * 10.0) / 10.0;
            }

            const char *jogDirection(Roof::End end)
            {
                return end == Roof::End::Open ? "open" : "close";
            }

            void writeMark(JsonObject out, const char *key, bool marked, const Stats &stats)
            {
                if (!marked || stats.empty())
                {
                    out[key] = nullptr;
                    return;
                }
                JsonObject m = out.createNestedObject(key);
                m["count"] = stats.count;
                m["mean"] = rounded1(stats.mean());
                m["min"] = stats.min;
                m["max"] = stats.max;
                m["noise"] = rounded1(stats.sigma());
            }

            void writeProposal(JsonObject out, const TeachProposal &p)
            {
                if (!p.made)
                {
                    out["proposal"] = nullptr;
                    return;
                }
                JsonObject o = out.createNestedObject("proposal");
                o["ok"] = p.ok;
                if (!p.ok)
                {
                    o["reason"] = p.problem.reason;
                    o["text"] = p.problem.text;
                    return;
                }
                o["hallHighEnd"] = Roof::endName(p.highEnd);
                o["hallHighCounts"] = p.highCounts;
                o["hallLowCounts"] = p.lowCounts;
            }

            bool parseEnd(const char *name, const char *open, const char *closed, Roof::End &out)
            {
                if (name == nullptr)
                    return false;
                if (std::strcmp(name, open) == 0)
                    out = Roof::End::Open;
                else if (std::strcmp(name, closed) == 0)
                    out = Roof::End::Closed;
                else
                    return false;
                return true;
            }

            bool fail(const char *&error, const char *text)
            {
                error = text;
                return false;
            }

            bool countsIn(JsonVariantConst v, uint16_t &out)
            {
                if (!v.is<int>() || v.as<int>() < 0 || v.as<int>() > 4095)
                    return false;
                out = static_cast<uint16_t>(v.as<int>());
                return true;
            }
        } // namespace

        void writeTeachDocument(JsonObject out, const TeachReport &r)
        {
            JsonObject jog = out.createNestedObject("jog");
            jog["active"] = r.jogActive;
            jog["id"] = r.jogId;
            if (r.jogActive)
                jog["direction"] = jogDirection(r.jogTarget);
            else
                jog["direction"] = nullptr;
            jog["elapsedMs"] = r.jogElapsedMs;
            jog["limitMs"] = r.jogLimitMs;
            jog["holdMs"] = Roof::JOG_HOLD_MS;
            jog["keepAliveMs"] = Roof::JOG_KEEPALIVE_MS;
            JsonObject rest = out.createNestedObject("rest");
            rest["ready"] = r.restReady;
            if (r.restReady)
                rest["counts"] = rounded1(r.restMean);
            else
                rest["counts"] = nullptr;
            JsonObject marks = out.createNestedObject("marks");
            for (Where w : {Where::Closed, Where::Open, Where::PartWay})
                writeMark(marks, whereName(w), r.marked[static_cast<unsigned>(w)], r.marks[static_cast<unsigned>(w)]);
            writeProposal(out, r.proposal);
        }

        bool parseJog(JsonVariantConst body, JogRequest &out, const char *&error)
        {
            if (!parseEnd(body["direction"].as<const char *>(), "open", "close", out.target))
                return fail(error, "direction must be open or close");
            if (!body["attended"].is<bool>())
                return fail(error, "attended must be true or false");
            out.attended = body["attended"].as<bool>();
            out.id = 0;
            JsonVariantConst id = body["id"];
            if (id.isNull())
                return true;
            if (!id.is<long long>() || id.as<long long>() < 1 || id.as<long long>() > static_cast<long long>(UINT32_MAX))
                return fail(error, "id must be a positive whole number");
            out.id = static_cast<uint32_t>(id.as<long long>());
            return true;
        }

        bool parseMark(JsonVariantConst body, Where &out, const char *&error)
        {
            if (parseWhere(body["at"].as<const char *>(), out))
                return true;
            error = "at must be closed, open or partWay";
            return false;
        }

        bool parseConfirm(JsonVariantConst body, TeachValues &out, const char *&error)
        {
            if (!parseEnd(body["hallHighEnd"].as<const char *>(), "open", "closed", out.highEnd))
                return fail(error, "hallHighEnd must be open or closed");
            if (!countsIn(body["hallHighCounts"], out.highCounts) || !countsIn(body["hallLowCounts"], out.lowCounts))
                return fail(error, "hallHighCounts and hallLowCounts must be whole numbers of counts");
            return true;
        }
    } // namespace Calib
} // namespace SQM
