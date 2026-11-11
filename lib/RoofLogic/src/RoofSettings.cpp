#include "RoofSettings.h"

#include <cmath>

namespace SQM
{
    namespace Roof
    {
        namespace
        {
            bool within(float value, float min, float max)
            {
                return std::isfinite(value) && value >= min && value <= max;
            }

            bool within(uint32_t value, uint32_t min, uint32_t max)
            {
                return value >= min && value <= max;
            }

            bool validMotion(const Settings &s)
            {
                using C = RoofConfig;
                return within(s.travelMm, C::MIN_TRAVEL_MM, C::MAX_TRAVEL_MM) &&
                       within(s.speedMmPerSecond, C::MIN_SPEED_MM_PER_SECOND, C::MAX_SPEED_MM_PER_SECOND) &&
                       within(s.timeoutMarginMs, C::MIN_TIMEOUT_MARGIN_SECONDS * 1000, C::MAX_TIMEOUT_MARGIN_SECONDS * 1000) &&
                       within(s.leaveMagnetMs, C::MIN_LEAVE_MAGNET_SECONDS * 1000, C::MAX_LEAVE_MAGNET_SECONDS * 1000) &&
                       s.rampUpMs <= C::MAX_RAMP_MS && s.rampDownMs <= C::MAX_RAMP_MS &&
                       within(s.topDutyPercent, C::MIN_TOP_DUTY_PERCENT, C::MAX_DUTY_PERCENT) &&
                       within(s.slowDutyPercent, C::MIN_SLOW_DUTY_PERCENT, C::MAX_DUTY_PERCENT) && s.slowDutyPercent <= s.topDutyPercent &&
                       (s.openDirection == Driver::Command::Forward || s.openDirection == Driver::Command::Reverse);
            }

            bool validSensing(const Settings &s)
            {
                using C = RoofConfig;
                return within(s.hallHighCounts, C::MIN_HALL_COUNTS, C::MAX_HALL_COUNTS) &&
                       within(s.hallLowCounts, C::MIN_HALL_COUNTS, C::MAX_HALL_COUNTS) &&
                       static_cast<uint32_t>(s.hallHighCounts) >= static_cast<uint32_t>(s.hallLowCounts) + C::MIN_HALL_GAP_COUNTS &&
                       (s.hallHighEnd == End::Open || s.hallHighEnd == End::Closed) &&
                       within(s.maxCurrentAmps, C::MIN_CURRENT_AMPS, C::MAX_CURRENT_AMPS) &&
                       within(s.minSupplyVolts, C::MIN_SUPPLY_VOLTS, C::MAX_SUPPLY_VOLTS);
            }

            // Out-of-range values become 0 so validSettings rejects them rather than
            // a narrowing cast wrapping them into range.
            uint8_t percent(uint32_t value)
            {
                return value <= RoofConfig::MAX_DUTY_PERCENT ? static_cast<uint8_t>(value) : 0;
            }

            uint16_t counts(uint32_t value)
            {
                return value <= RoofConfig::MAX_HALL_COUNTS ? static_cast<uint16_t>(value) : 0;
            }

            uint32_t secondsToMs(uint32_t seconds)
            {
                constexpr uint32_t LIMIT = UINT32_MAX / 1000;
                return seconds <= LIMIT ? seconds * 1000 : 0;
            }
        } // namespace

        bool supplyReadable(float volts)
        {
            return std::isfinite(volts) && volts <= MAX_SUPPLY_READING_VOLTS;
        }

        bool currentReadable(float amps)
        {
            return std::isfinite(amps) && amps >= MIN_CURRENT_READING_AMPS;
        }

        bool validSettings(const Settings &settings)
        {
            return validMotion(settings) && validSensing(settings);
        }

        Settings settingsFromConfig(const RoofConfig &config)
        {
            Settings s;
            s.travelMm = config.travelMm;
            s.speedMmPerSecond = config.speedMmPerSecond;
            s.timeoutMarginMs = secondsToMs(config.timeoutMarginSeconds);
            s.leaveMagnetMs = secondsToMs(config.leaveMagnetSeconds);
            s.rampUpMs = config.rampUpMs;
            s.rampDownMs = config.rampDownMs;
            s.topDutyPercent = percent(config.topDutyPercent);
            s.slowDutyPercent = percent(config.slowDutyPercent);
            s.openDirection = config.openDirection == RoofConfig::Direction::Reverse ? Driver::Command::Reverse : Driver::Command::Forward;
            s.hallHighEnd = config.hallHighEnd == RoofConfig::HallEnd::Closed ? End::Closed : End::Open;
            s.hallHighCounts = counts(config.hallHighCounts);
            s.hallLowCounts = counts(config.hallLowCounts);
            s.currentSenseEnabled = config.currentSenseEnabled;
            s.maxCurrentAmps = config.maxCurrentAmps;
            s.minSupplyVolts = config.minSupplyVolts;
            return s;
        }
    } // namespace Roof
} // namespace SQM
