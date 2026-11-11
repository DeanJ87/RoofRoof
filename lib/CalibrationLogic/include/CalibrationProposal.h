#pragma once

// Spec 016 (research R5, R7): what a finished calibration run measured and
// the settings it proposes from it. Pure: the measurements and the settings
// that were saved at the start go in, the proposal comes out.

#include <cstdint>
#include <string>
#include <vector>

#include "CalibrationStats.h"
#include "RoofSettings.h"
#include "RoofTypes.h"

namespace SQM
{
    namespace Calib
    {
        enum class LegKind : uint8_t
        {
            ToClosed,  // from where the roof is to the closed end (not measured)
            SlowOpen,  // a full travel at the slow duty
            SlowClose, // back at the slow duty
            TopOpen,   // the top-duty pass
            TopClose,
        };

        const char *legName(LegKind kind); // "toClosed", "slowOpen", ...

        // An end must read at least this far from the in-between level.
        constexpr double MIN_END_SEPARATION_COUNTS = 300.0;
        // A threshold at least this far (or 4 x the noise) beyond the in-between readings.
        constexpr double MIN_CLEARANCE_COUNTS = 100.0;
        constexpr double CLEARANCE_SIGMAS = 4.0;
        // An end's lowest reading at rest at least this far (or 3 x its rest noise) past its threshold.
        constexpr double MIN_REST_MARGIN_COUNTS = 10.0;
        constexpr double REST_MARGIN_SIGMAS = 3.0;
        // A measured leg shorter than this isn't a full travel.
        constexpr uint32_t MIN_MEASURED_LEG_MS = 3000;
        // The open and close legs may differ by this much before the speed is withheld (or warned).
        constexpr double MAX_LEG_DISAGREEMENT = 0.30;
        constexpr double WARN_LEG_DISAGREEMENT = 0.15;
        // The margin: at least this, 20 % of a full move, and 1.5 x the shortfall plus 3 s.
        constexpr uint32_t MIN_PROPOSED_MARGIN_MS = 5000;
        constexpr double MARGIN_MOVE_FRACTION = 0.2;
        constexpr double MARGIN_SHORTFALL_FACTOR = 1.5;
        constexpr uint32_t MARGIN_SHORTFALL_EXTRA_MS = 3000;
        // A peak running current at this fraction of the limit is warned about.
        constexpr float CURRENT_WARN_FRACTION = 0.8f;

        struct MeasuredLeg
        {
            LegKind kind = LegKind::SlowOpen;
            Roof::End target = Roof::End::Open;
            uint8_t dutyPercent = 0;
            uint32_t drivenMs = 0;
            float dutyMs = 0.0f;
            Roof::HallLevel level = Roof::HallLevel::Unsettled;
            Electrical electrical;
        };

        constexpr uint8_t MAX_MEASURED_LEGS = 4;

        struct Measurements
        {
            MeasuredLeg legs[MAX_MEASURED_LEGS];
            uint8_t legCount = 0;
            Stats between;    // the middle half of each measured leg
            Stats closedRest; // at rest at the closed end
            Stats openRest;
            Roof::HallLevel closedLevel = Roof::HallLevel::Unsettled;
            Roof::HallLevel openLevel = Roof::HallLevel::Unsettled;
            uint32_t travelMm = 0;
            bool currentSense = false;
            float maxCurrentAmps = 0.0f;
        };

        // A value withheld, or a warning: a machine name and its text ("" = none).
        struct Problem
        {
            std::string reason;
            std::string text;
        };

        struct Proposal
        {
            uint32_t travelMm = 0;
            bool speed = false; // speedMmPerSecond and timeoutMarginSeconds proposed
            float speedMmPerSecond = 0.0f;
            uint32_t timeoutMarginSeconds = 0;
            Problem speedProblem;
            bool hall = false; // hallHighEnd and both thresholds proposed
            Roof::End hallHighEnd = Roof::End::Open;
            uint16_t hallHighCounts = 0;
            uint16_t hallLowCounts = 0;
            Problem hallProblem;
            std::vector<Problem> warnings;
        };

        // The speed a measured leg implies at 100 % duty (travel ÷ its duty-weighted time).
        float legSpeed(const MeasuredLeg &leg, uint32_t travelMm);

        Proposal propose(const Measurements &measured, const Roof::Settings &old);
    } // namespace Calib
} // namespace SQM
