#include "DiagDocuments.h"

#include <algorithm>

#include "MotionProfile.h"

// The live document (contracts/diagnostics.md, GET /api/diag).

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

            void putCount(JsonObject out, const char *key, int64_t value)
            {
                if (value < 0)
                    out[key] = nullptr;
                else
                    out[key] = value;
            }

            uint32_t budgetUsed(const RoofLive &roof)
            {
                if (moving(roof.status.state))
                    return roof.budgetUsedMs[static_cast<size_t>(roof.status.target)];
                return std::max(roof.budgetUsedMs[0], roof.budgetUsedMs[1]);
            }

            void writeMotion(JsonObject out, const RoofLive &roof)
            {
                const Roof::Status &st = roof.status;
                const bool movingNow = moving(st.state);
                out["positionKnown"] = st.positionKnown;
                putReading(out, "positionMm", st.positionKnown ? st.positionMm : NAN, 1);
                putCount(out, "elapsedMs", movingNow ? static_cast<int64_t>(st.moveElapsedMs) : UNKNOWN);
                putCount(out, "remainingMs", remainingMs(st, roof.settings));
                putCount(out, "timeoutMs", movingNow ? static_cast<int64_t>(st.moveTimeoutMs) : UNKNOWN);
                out["driveBudgetUsedMs"] = budgetUsed(roof);
                out["driveBudgetMs"] = Roof::driveBudgetMs(roof.settings);
            }

            void writeRoof(JsonObject out, const RoofLive &roof)
            {
                const Roof::Status &st = roof.status;
                out["state"] = Roof::stateName(st.state);
                if (moving(st.state))
                    out["target"] = Roof::endName(st.target);
                else
                    out["target"] = nullptr;
                out["phase"] = phaseName(roof.log.phase);
                out["driving"] = st.driving;
                out["direction"] = directionName(roof.output.command);
                out["duty"] = roof.output.duty;
                out["dutyPercent"] = static_cast<int>(std::lround(roof.output.duty * 100.0 / 255.0));
                out["en"] = roof.pins.enable;
                out["rpwm"] = roof.pins.rpwm;
                out["lpwm"] = roof.pins.lpwm;
                const float volts = Roof::supplyReadable(roof.supplyVolts) ? roof.supplyVolts : NAN;
                putReading(out, "supplyVolts", volts, 2);
                putReading(out, "restVolts", roof.log.restVolts, 2);
                putReading(out, "sagVolts", roof.log.sagVolts, 2);
                out["currentSense"] = roof.settings.currentSenseEnabled;
                const float amps = roof.settings.currentSenseEnabled ? roof.motorAmps : NAN;
                putReading(out, "motorAmps", amps, 2);
                putReading(out, "powerWatts", volts * amps, 1);
                out["hallCounts"] = st.hallCounts;
                out["hall"] = Roof::hallLevelName(st.hall);
                writeMotion(out, roof);
            }

            void writeControl(JsonObject out, const RoofLive &roof, const SystemLive &system)
            {
                out["periodMs"] = system.periodMs;
                putCount(out, "stepUs", system.stepUs);
                putCount(out, "maxStepUs", system.maxStepUs);
                out["maxGapMs"] = roof.maxGapMs;
                out["watchdogLimitMs"] = system.watchdogLimitMs;
                out["watchdogMarginMs"] = static_cast<int64_t>(system.watchdogLimitMs) - static_cast<int64_t>(roof.maxGapMs);
                out["maxBeatAgeMs"] = roof.maxBeatAgeMs;
                out["watchdogTrips"] = roof.watchdogTrips;
            }

            void writeSystem(JsonObject out, const SystemLive &system)
            {
                JsonObject loop = out.createNestedObject("loop");
                putCount(loop, "passMs", system.passMs);
                putCount(loop, "maxPassMs", system.maxPassMs);
                JsonObject memory = out.createNestedObject("memory");
                putCount(memory, "freeHeap", system.freeHeap);
                putCount(memory, "minFreeHeap", system.minFreeHeap);
                putCount(memory, "largestBlock", system.largestBlock);
                JsonObject stack = memory.createNestedObject("stackFree");
                putCount(stack, "control", system.stackControl);
                putCount(stack, "loop", system.stackLoop);
                putCount(stack, "network", system.stackNetwork);
                JsonObject wifi = out.createNestedObject("wifi");
                if (system.rssi == UNKNOWN)
                    wifi["rssi"] = nullptr;
                else
                    wifi["rssi"] = system.rssi; // dBm, negative
                out["uptimeS"] = system.uptimeS;
                if (system.resetReason != nullptr)
                    out["resetReason"] = system.resetReason;
                else
                    out["resetReason"] = nullptr;
            }
        } // namespace

        bool HeldIds::holds(uint32_t id) const
        {
            for (uint8_t i = 0; i < count; ++i)
                if (ids[i] == id)
                    return true;
            return false;
        }

        void writeOff(JsonObject out)
        {
            out["enabled"] = false;
        }

        int64_t remainingMs(const Roof::Status &status, const Roof::Settings &settings)
        {
            if (!moving(status.state) || !status.positionKnown)
                return UNKNOWN;
            const float travel = static_cast<float>(settings.travelMm);
            const float left = status.target == Roof::End::Open ? travel - status.positionMm : status.positionMm;
            return Roof::expectedMoveMs(settings, std::max(0.0f, left));
        }

        void writeLive(JsonObject out, const RoofLive &roof, const SystemLive &system)
        {
            out["enabled"] = true;
            out["log"] = system.logReady ? "ready" : "unavailable";
            out["simulation"] = system.simulation;
            writeRoof(out.createNestedObject("roof"), roof);
            writeControl(out.createNestedObject("control"), roof, system);
            writeSystem(out, system);
            if (!system.logReady || roof.log.moveId == 0)
            {
                out["move"] = nullptr;
                return;
            }
            JsonObject move = out.createNestedObject("move");
            move["id"] = roof.log.moveId;
            move["active"] = roof.log.moveActive;
        }
    } // namespace Diag
} // namespace SQM
