#include "MotionProfile.h"

#include <algorithm>
#include <cmath>

namespace SQM
{
    namespace Roof
    {
        namespace
        {
            // The ramp-up is integrated in steps this long (at most 3000 of them for
            // the longest ramp); the rest of the move has a closed form (research R4).
            constexpr uint32_t INTEGRATION_STEP_MS = 10;
            constexpr double MAX_DUTY = 255.0;

            // mm per ms at 100% duty.
            double fullSpeedMmPerMs(const Settings &settings)
            {
                return static_cast<double>(settings.speedMmPerSecond) / 1000.0;
            }

            // The length of the slow-down zone: the distance covered at the top duty
            // during the ramp-down time.
            double slowZoneMm(const Settings &settings)
            {
                return fullSpeedMmPerMs(settings) * dutyFraction(settings.topDutyPercent) * settings.rampDownMs;
            }

            uint32_t clampMs(double ms)
            {
                if (!(ms > 0.0))
                    return 0;
                constexpr double LIMIT = static_cast<double>(UINT32_MAX);
                return ms >= LIMIT ? UINT32_MAX : static_cast<uint32_t>(std::ceil(ms));
            }

            uint32_t plusMargin(uint32_t ms, uint32_t marginMs)
            {
                return ms > UINT32_MAX - marginMs ? UINT32_MAX : ms + marginMs;
            }

            // Time to cover `remainingMm` once the ramp-up is over: cruise at the top
            // duty to the zone, then the zone, where the fraction falls linearly with
            // the distance left (an exponential approach that ends at the slow duty).
            double timeAfterRampUpMs(const Settings &settings, double remainingMm)
            {
                const double speed = fullSpeedMmPerMs(settings);
                const double top = dutyFraction(settings.topDutyPercent);
                const double slow = dutyFraction(settings.slowDutyPercent);
                const double zone = slowZoneMm(settings);
                double ms = 0.0;
                if (remainingMm > zone)
                {
                    ms += (remainingMm - zone) / (speed * top);
                    remainingMm = zone;
                }
                if (remainingMm <= 0.0)
                    return ms;
                if (top - slow < 1e-6)
                    return ms + remainingMm / (speed * top);
                const double startFraction = slow + (top - slow) * remainingMm / zone;
                return ms + zone / (speed * (top - slow)) * std::log(startFraction / slow);
            }
        } // namespace

        float dutyFraction(uint8_t percent)
        {
            return static_cast<float>(std::min<uint8_t>(percent, 100)) / 100.0f;
        }

        uint8_t toDuty(float fraction)
        {
            if (!(fraction > 0.0f))
                return 0;
            if (fraction >= 1.0f)
                return 255;
            return static_cast<uint8_t>(std::lround(fraction * MAX_DUTY));
        }

        float profileFraction(const Settings &settings, uint32_t drivenMs, float remainingMm)
        {
            const float top = dutyFraction(settings.topDutyPercent);
            const float slow = dutyFraction(settings.slowDutyPercent);
            if (!(remainingMm > 0.0f))
                return slow;
            float up = top;
            if (drivenMs < settings.rampUpMs)
                up = slow + (top - slow) * static_cast<float>(drivenMs) / static_cast<float>(settings.rampUpMs);
            const double zone = slowZoneMm(settings);
            float down = top;
            if (zone > 0.0 && remainingMm < zone)
                down = slow + (top - slow) * static_cast<float>(remainingMm / zone);
            return std::max(slow, std::min(up, down));
        }

        float legFraction(const Settings &settings, const CalibrationLeg &leg, uint32_t drivenMs)
        {
            const float slow = dutyFraction(settings.slowDutyPercent);
            const float cruise = dutyFraction(leg.dutyPercent);
            if (cruise <= slow)
                return cruise;
            float up = cruise;
            if (drivenMs < settings.rampUpMs)
                up = slow + (cruise - slow) * static_cast<float>(drivenMs) / static_cast<float>(settings.rampUpMs);
            float down = cruise;
            if (leg.slowAfterMs > 0 && drivenMs >= leg.slowAfterMs)
            {
                const uint32_t intoMs = drivenMs - leg.slowAfterMs;
                down = intoMs >= settings.rampDownMs
                           ? slow
                           : cruise - (cruise - slow) * static_cast<float>(intoMs) / static_cast<float>(settings.rampDownMs);
            }
            return std::max(slow, std::min(up, down));
        }

        float advanceMm(const Settings &settings, uint8_t duty, uint32_t elapsedMs)
        {
            return static_cast<float>(fullSpeedMmPerMs(settings) * (duty / MAX_DUTY) * elapsedMs);
        }

        uint32_t expectedMoveMs(const Settings &settings, float distanceMm)
        {
            if (!(distanceMm > 0.0f) || !validSettings(settings))
                return 0;
            const double speed = fullSpeedMmPerMs(settings);
            double coveredMm = 0.0;
            uint32_t elapsedMs = 0;
            while (elapsedMs < settings.rampUpMs && coveredMm < distanceMm)
            {
                const float fraction = profileFraction(settings, elapsedMs, static_cast<float>(distanceMm - coveredMm));
                coveredMm += speed * fraction * INTEGRATION_STEP_MS;
                elapsedMs += INTEGRATION_STEP_MS;
            }
            if (coveredMm >= distanceMm)
                return elapsedMs;
            return clampMs(elapsedMs + timeAfterRampUpMs(settings, distanceMm - coveredMm));
        }

        uint32_t moveTimeoutMs(const Settings &settings, float distanceMm)
        {
            return plusMargin(expectedMoveMs(settings, distanceMm), settings.timeoutMarginMs);
        }

        uint32_t unknownMoveTimeoutMs(const Settings &settings)
        {
            if (!validSettings(settings))
                return settings.timeoutMarginMs;
            const double slowSpeed = fullSpeedMmPerMs(settings) * dutyFraction(settings.slowDutyPercent);
            return plusMargin(clampMs(settings.travelMm / slowSpeed), settings.timeoutMarginMs);
        }

        uint32_t driveBudgetMs(const Settings &settings)
        {
            return unknownMoveTimeoutMs(settings);
        }
    } // namespace Roof
} // namespace SQM
