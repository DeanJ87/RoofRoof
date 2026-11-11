#include "RoofPlant.h"

#include <algorithm>
#include <cmath>

namespace SQM
{
    namespace RoofPlant
    {
        float Plant::drive(const Roof::Output &output, uint32_t elapsedMs)
        {
            lastOutput = output;
            if (output.command == Driver::Command::Off || output.duty == 0 || jammed)
                return 0.0f;
            float direction = output.command == Driver::Command::Forward ? 1.0f : -1.0f;
            if (reversedWiring)
                direction = -direction;
            const float before = positionMm;
            const float duty = output.duty / 255.0f;
            const float d = deadbandFraction;
            const float effective = d > 0.0f ? (duty <= d ? 0.0f : (duty - d) / (1.0f - d)) : duty;
            const float step = speedMmPerSecond / 1000.0f * effective * static_cast<float>(elapsedMs);
            positionMm = std::min(travelMm, std::max(0.0f, positionMm + direction * step));
            return positionMm - before;
        }

        bool Plant::atClosedMagnet() const
        {
            return !closedMagnetMissing && positionMm <= magnetZoneMm;
        }

        bool Plant::atOpenMagnet() const
        {
            return !openMagnetMissing && positionMm >= travelMm - magnetZoneMm;
        }

        uint16_t Plant::hallCounts() const
        {
            if (railCounts >= 0)
                return static_cast<uint16_t>(railCounts);
            const uint16_t openCounts = highEndClosed ? lowMagnetCounts : highMagnetCounts;
            const uint16_t closedCounts = highEndClosed ? highMagnetCounts : lowMagnetCounts;
            if (atOpenMagnet())
                return openCounts;
            if (atClosedMagnet())
                return closedCounts;
            const bool nearClosed = positionMm < travelMm / 2.0f;
            const bool lowAtClosed = !highEndClosed;
            const bool nearLow = nearClosed == lowAtClosed;
            const uint16_t edge = nearLow ? restingNearLow : restingNearHigh;
            return edge != 0 ? edge : restingCounts;
        }

        float Plant::motorAmps() const
        {
            if (!currentFitted)
                return std::nanf("");
            if (lastOutput.command == Driver::Command::Off || lastOutput.duty == 0)
                return 0.0f;
            const bool atStop = positionMm <= 0.0f || positionMm >= travelMm;
            return jammed || atStop ? stallAmps : runningAmps;
        }

        float Plant::senseAmps() const
        {
            const float amps = motorAmps();
            if (!std::isfinite(amps) || lastOutput.command == Driver::Command::Off)
                return amps;
            return amps * (lastOutput.duty / 255.0f);
        }

        float Plant::supplyNow() const
        {
            if (lastOutput.command == Driver::Command::Off || lastOutput.duty == 0)
                return supplyVolts;
            return supplyVolts - sagVolts * (lastOutput.duty / 255.0f);
        }

        Roof::Readings Plant::readings(uint32_t nowMs) const
        {
            Roof::Readings r;
            r.nowMs = nowMs;
            r.hallCounts = hallCounts();
            r.supplyVolts = supplyNow();
            r.motorAmps = motorAmps();
            return r;
        }

        void applySettings(Plant &plant, const Roof::Settings &settings, float minRunningAmps)
        {
            plant.travelMm = static_cast<float>(settings.travelMm);
            plant.speedMmPerSecond = settings.speedMmPerSecond;
            plant.positionMm = std::min(plant.travelMm, std::max(0.0f, plant.positionMm));
            const uint32_t low = settings.hallLowCounts;
            const uint32_t high = settings.hallHighCounts;
            plant.lowMagnetCounts = static_cast<uint16_t>((low + Roof::HALL_RAIL_LOW_COUNTS) / 2);
            plant.highMagnetCounts = static_cast<uint16_t>((high + Roof::HALL_RAIL_HIGH_COUNTS) / 2);
            plant.restingCounts = static_cast<uint16_t>((low + high) / 2);
            // A value Between must be past the band of the magnet just left
            // (HallFilter: a magnet is kept within HALL_HYSTERESIS_COUNTS).
            const uint32_t hyst = Roof::HALL_HYSTERESIS_COUNTS;
            const bool midClear = plant.restingCounts > low + hyst && plant.restingCounts + hyst < high;
            plant.restingNearLow = midClear ? 0 : static_cast<uint16_t>(std::min(high - 1, low + hyst + 1));
            plant.restingNearHigh = midClear ? 0 : static_cast<uint16_t>(std::max(low + 1, high - hyst - 1));
            plant.highEndClosed = settings.hallHighEnd == Roof::End::Closed;
            plant.reversedWiring = settings.openDirection == Driver::Command::Reverse;
            plant.currentFitted = settings.currentSenseEnabled;
            plant.runningAmps = std::min(settings.maxCurrentAmps * 0.9f, std::max(settings.maxCurrentAmps * 0.3f, minRunningAmps * 1.2f));
            plant.stallAmps = settings.maxCurrentAmps * 1.5f;
            plant.sagVolts = SETTINGS_SAG_VOLTS;
        }

        Plant forSettings(const Roof::Settings &settings, float startMm, float minRunningAmps)
        {
            Plant plant;
            plant.positionMm = startMm;
            applySettings(plant, settings, minRunningAmps);
            return plant;
        }

        Roof::Output outputOf(const Driver::Outputs &levels)
        {
            Roof::Output out;
            if (!levels.enable || (levels.rpwm > 0 && levels.lpwm > 0))
                return out;
            if (levels.rpwm > 0)
            {
                out.command = Driver::Command::Forward;
                out.duty = levels.rpwm;
            }
            else if (levels.lpwm > 0)
            {
                out.command = Driver::Command::Reverse;
                out.duty = levels.lpwm;
            }
            return out;
        }
    } // namespace RoofPlant
} // namespace SQM
