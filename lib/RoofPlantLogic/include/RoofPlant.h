#pragma once

// A simulated roof (spec 011, data-model.md): the motor moves the roof at a
// speed proportional to the duty, the rubber end stops hold it at either end,
// and the Hall sensor reads a magnet near each end. It reports the motor
// current and a 12 V supply that sags a little under load. Faults can be
// switched on: a jam, a motor wired backwards, a missing magnet, a sensor at a
// rail, a sagging supply. It decides nothing: it only follows the output it is
// given. Used by the native tests (spec 002), the Alpaca simulator (spec 006),
// the demo and bench mode (spec 011).

#include <cstdint>

#include "DriverLogic.h"
#include "RoofSettings.h"
#include "RoofTypes.h"

namespace SQM
{
    namespace RoofPlant
    {
        struct Plant
        {
            float travelMm = 1850.0f;
            float speedMmPerSecond = 38.0f; // the real roof's speed at 100% duty
            float positionMm = 0.0f;        // 0 closed
            float magnetZoneMm = 25.0f;     // the Hall sees a magnet this close to an end
            uint16_t restingCounts = 2050;
            // Between the magnets, near the low magnet's end and near the high
            // magnet's end: 0 uses restingCounts. Set when the thresholds are so
            // close that no single value is clear of both hysteresis bands.
            uint16_t restingNearLow = 0;
            uint16_t restingNearHigh = 0;
            uint16_t highMagnetCounts = 3400; // the open end's magnet unless highEndClosed
            uint16_t lowMagnetCounts = 700;
            bool highEndClosed = false;
            bool jammed = false;
            bool reversedWiring = false; // Forward drives toward closed
            bool openMagnetMissing = false;
            bool closedMagnetMissing = false;
            int railCounts = -1; // >= 0: the Hall reads this whatever the position
            float supplyVolts = 12.6f;
            float sagVolts = 0.0f; // the supply drops this much at full duty while driving
            float runningAmps = 3.0f;
            float stallAmps = 18.0f;
            bool currentFitted = true;

            // Moves the roof for `elapsedMs` under `output`; returns the distance moved.
            float drive(const Roof::Output &output, uint32_t elapsedMs);

            bool atClosedMagnet() const;
            bool atOpenMagnet() const;
            uint16_t hallCounts() const;
            // Pushing into an end stop or a jam is a stall. NaN when not fitted.
            float motorAmps() const;
            // What the IBT-2's current sense averages to under PWM: the motor
            // current times the duty (RoofIo::runningAmps scales it back).
            // NaN when not fitted, 0 while off.
            float senseAmps() const;
            // The supply, less the sag while the motor is driven.
            float supplyNow() const;
            Roof::Readings readings(uint32_t nowMs) const;

            Roof::Output lastOutput;
        };

        // Sag at full duty for a plant set up from the settings (research R5).
        constexpr float SETTINGS_SAG_VOLTS = 0.3f;

        // A plant that matches `settings`: the travel and speed; the Hall
        // counts halfway between each threshold and its rail, and halfway
        // between the thresholds in between; which end is high; the open
        // direction; current sense, with running 30% and stall 150% of the
        // current limit. At `startMm`.
        Plant forSettings(const Roof::Settings &settings, float startMm, float minRunningAmps = 0.0f);

        // Sets `plant` up for new `settings`, keeping its position (clamped
        // to the new travel) and its injected jam, missing magnets, rail and
        // supply. The wiring follows the open-direction setting. Running
        // current is at least 1.2 x `minRunningAmps` (the "motor not drawing"
        // limit), so a legal setting never reads as no current.
        void applySettings(Plant &plant, const Roof::Settings &settings, float minRunningAmps = 0.0f);

        // The driver command a set of pin levels drives the motor with (what
        // the bridge would do): Off unless EN is high and exactly one PWM is
        // above 0.
        Roof::Output outputOf(const Driver::Outputs &levels);
    } // namespace RoofPlant
} // namespace SQM
