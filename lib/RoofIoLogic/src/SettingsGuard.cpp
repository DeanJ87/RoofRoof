#include "SettingsGuard.h"

namespace SQM
{
    namespace RoofIo
    {
        namespace
        {
            bool roofDiffers(const RoofConfig &a, const RoofConfig &b)
            {
                return a.travelMm != b.travelMm || a.speedMmPerSecond != b.speedMmPerSecond ||
                       a.timeoutMarginSeconds != b.timeoutMarginSeconds || a.leaveMagnetSeconds != b.leaveMagnetSeconds ||
                       a.rampUpMs != b.rampUpMs || a.rampDownMs != b.rampDownMs || a.topDutyPercent != b.topDutyPercent ||
                       a.slowDutyPercent != b.slowDutyPercent || a.openDirection != b.openDirection ||
                       a.hallHighEnd != b.hallHighEnd || a.hallHighCounts != b.hallHighCounts ||
                       a.hallLowCounts != b.hallLowCounts || a.currentSenseEnabled != b.currentSenseEnabled ||
                       a.maxCurrentAmps != b.maxCurrentAmps || a.currentAmpsPerVolt != b.currentAmpsPerVolt ||
                       a.minSupplyVolts != b.minSupplyVolts;
            }

            bool parkDiffers(const ParkConfig &a, const ParkConfig &b)
            {
                return a.ninaUrl != b.ninaUrl || a.ninaApi != b.ninaApi || a.switches != b.switches;
            }
        } // namespace

        bool motionSettingsChanged(const Config &current, const Config &next)
        {
            return roofDiffers(current.roof, next.roof) || parkDiffers(current.park, next.park);
        }

        bool settingsSaveRefused(Roof::State state, bool motionChanged)
        {
            return motionChanged && (state == Roof::State::Opening || state == Roof::State::Closing);
        }
    } // namespace RoofIo
} // namespace SQM
