#pragma once

// Advanced diagnostics (spec 017, data-model.md): what one control step hands
// the recorder, a move's summary and its downsampled profile. Plain values:
// the control task fills a Sample every step; the recorder (MoveLog) and the
// documents only read them. Nothing here feeds a motion decision (FR-024).

#include <cmath>
#include <cstdint>

#include "RoofTypes.h"

namespace SQM
{
    namespace Diag
    {
        // Moves held with their profiles (the one in progress counts).
        constexpr uint8_t MAX_MOVES = 5;
        // Profile points per move; full, neighbours merge and the interval doubles.
        constexpr uint8_t MAX_POINTS = 120;
        // A profile point's first interval: 4 points a second.
        constexpr uint32_t FIRST_INTERVAL_MS = 250;
        // The supply at rest is the average of a block this long with the motor off.
        constexpr uint32_t REST_BLOCK_MS = 1000;
        // Driven current readings averaged for a fault's stall current (13 x 20 ms).
        constexpr uint8_t STALL_READINGS = 13;
        // A point value with no reading.
        constexpr uint16_t NONE = 0xFFFF;
        // An integer value that can't be read (written as null).
        constexpr int64_t UNKNOWN = -1;

        // Where the duty is in the move's profile (research R5).
        enum class Phase : uint8_t
        {
            Idle,      // not opening or closing
            DeadTime,  // moving, the driver not yet on
            Slow,      // a move from an unknown position, at the slow duty
            SoftStart, // rising from the slow duty
            Cruise,    // at the top duty
            Slowing,   // falling toward the end
            Creep,     // at the slow duty after slowing
        };

        // How a move ended.
        enum class Result : uint8_t
        {
            Arrived, // at its target's magnet
            Stopped, // stopped part-way (a stop, an abort, a reversal)
            Fault,   // a motion fault stopped it
        };

        // One control step, as the step saw it after applying its output.
        struct Sample
        {
            uint32_t nowMs = 0;
            Roof::Status status;
            Roof::Output output;
            float supplyVolts = NAN; // NaN: unreadable
            float motorAmps = NAN;   // the running current; NaN without current sense
            Roof::Origin origin = Roof::Origin::Person; // of the last accepted open or close
            uint8_t slowDuty = 0; // the settings' duties, 0-255
            uint8_t topDuty = 255;
        };

        // One profile interval (8 bytes).
        struct Point
        {
            uint16_t ampsAvg = NONE;  // centiamps
            uint16_t ampsMax = NONE;  // centiamps
            uint16_t voltsMin = NONE; // centivolts
            uint8_t duty = 0;         // average, 0-255
        };

        // A move's summary (FR-009). Floats are NaN when there is no reading.
        struct Summary
        {
            uint32_t id = 0;
            uint32_t startMs = 0; // uptime (a kept summary: 0)
            uint32_t endMs = 0;   // the last step so far while active (a kept summary: the duration)
            uint32_t startEpoch = 0; // Unix seconds, kept summaries only (0: clock not set)
            uint32_t endEpoch = 0;
            uint32_t drivenMs = 0;
            float faultMeasured = 0.0f;
            float faultLimit = 0.0f;
            float peakAmps = NAN;
            float averageAmps = NAN;
            float stallAmps = NAN;
            float restVolts = NAN;
            float minVolts = NAN;
            uint16_t hallStart = 0;
            uint16_t hallEnd = 0;
            Roof::Origin origin = Roof::Origin::Person;
            Roof::End target = Roof::End::Closed;
            Roof::State endState = Roof::State::Unknown;
            Result result = Result::Stopped;
            Roof::FaultKind fault = Roof::FaultKind::None;
            bool active = false;
            bool kept = false; // loaded from the kept file: no uptime, no profile
            bool fromUnknown = false;

            uint32_t durationMs() const { return endMs - startMs; }
            float sagVolts() const { return restVolts - minVolts; } // NaN unless both are known
        };

        // A held move: its summary and profile. Point i covers
        // [i * intervalMs, (i + 1) * intervalMs) from the start; the last may be shorter.
        struct Move
        {
            Summary summary;
            uint32_t intervalMs = FIRST_INTERVAL_MS;
            uint8_t count = 0;
            Point points[MAX_POINTS];
        };

        // The device's clock when a document is written: uptime now and Unix
        // seconds now (0: the clock isn't set). Uptimes become epochs through it.
        struct Clock
        {
            uint32_t nowMs = 0;
            uint32_t epoch = 0;
        };
        // `ms` (uptime) as Unix seconds, or 0 when the clock isn't set.
        uint32_t epochOf(const Clock &clock, uint32_t ms);

        // A point's values in physical units (NaN when none): the read interface
        // for other firmware code (research R13).
        float pointAmpsAvg(const Point &point);
        float pointAmpsMax(const Point &point);
        float pointVolts(const Point &point);
        float pointDutyPercent(const Point &point);
        uint32_t pointStartMs(const Move &move, uint8_t index);
        // A reading as a point value: rounded to hundredths, NONE when it isn't
        // finite or is outside 0-655.34.
        uint16_t toHundredths(float value);

        // Stable names for the documents (never translated).
        const char *phaseName(Phase phase);
        const char *resultName(Result result);
        const char *originName(Roof::Origin origin);
        const char *directionName(Driver::Command command);

        // The roof is opening or closing.
        inline bool moving(Roof::State state)
        {
            return state == Roof::State::Opening || state == Roof::State::Closing;
        }
    } // namespace Diag
} // namespace SQM
