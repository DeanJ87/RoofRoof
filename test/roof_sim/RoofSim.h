#pragma once

// A simulated roof for the motion logic's tests (spec 002): the motor moves the
// roof at a speed proportional to the duty, the rubber end stops hold it at
// either end, and the Hall sensor reads a magnet near each end. Faults can be
// switched on: a jam, a motor wired backwards, a missing magnet, a sensor at a
// rail, a sagging supply, and the motor current.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "DriverLogic.h"
#include "RoofController.h"
#include "RoofTypes.h"

namespace RoofTest
{
    using SQM::Driver::Command;

    struct RoofSim
    {
        float travelMm = 1850.0f;
        float speedMmPerSecond = 38.0f; // the real roof's speed at 100% duty
        float positionMm = 0.0f;        // 0 closed
        float magnetZoneMm = 25.0f;     // the Hall sees a magnet this close to an end
        uint16_t restingCounts = 2050;
        uint16_t highMagnetCounts = 3400; // the open end's magnet unless highEndClosed
        uint16_t lowMagnetCounts = 700;
        bool highEndClosed = false;
        bool jammed = false;
        bool reversedWiring = false; // Forward drives toward closed
        bool openMagnetMissing = false;
        bool closedMagnetMissing = false;
        int railCounts = -1; // >= 0: the Hall reads this whatever the position
        float supplyVolts = 12.6f;
        float runningAmps = 3.0f;
        float stallAmps = 18.0f;
        bool currentFitted = true;

        // Moves the roof for `elapsedMs` under `output`; returns the distance moved.
        float drive(const SQM::Roof::Output &output, uint32_t elapsedMs)
        {
            lastOutput = output;
            if (output.command == Command::Off || output.duty == 0 || jammed)
                return 0.0f;
            float direction = output.command == Command::Forward ? 1.0f : -1.0f;
            if (reversedWiring)
                direction = -direction;
            const float before = positionMm;
            const float step = speedMmPerSecond / 1000.0f * (output.duty / 255.0f) * static_cast<float>(elapsedMs);
            positionMm = std::min(travelMm, std::max(0.0f, positionMm + direction * step));
            return positionMm - before;
        }

        bool atClosedMagnet() const { return !closedMagnetMissing && positionMm <= magnetZoneMm; }
        bool atOpenMagnet() const { return !openMagnetMissing && positionMm >= travelMm - magnetZoneMm; }

        uint16_t hallCounts() const
        {
            if (railCounts >= 0)
                return static_cast<uint16_t>(railCounts);
            const uint16_t openCounts = highEndClosed ? lowMagnetCounts : highMagnetCounts;
            const uint16_t closedCounts = highEndClosed ? highMagnetCounts : lowMagnetCounts;
            if (atOpenMagnet())
                return openCounts;
            if (atClosedMagnet())
                return closedCounts;
            return restingCounts;
        }

        // Pushing into an end stop or a jam is a stall.
        float motorAmps() const
        {
            if (!currentFitted)
                return std::nanf("");
            if (lastOutput.command == Command::Off || lastOutput.duty == 0)
                return 0.0f;
            const bool atStop = positionMm <= 0.0f || positionMm >= travelMm;
            return jammed || atStop ? stallAmps : runningAmps;
        }

        SQM::Roof::Readings readings(uint32_t nowMs) const
        {
            SQM::Roof::Readings r;
            r.nowMs = nowMs;
            r.hallCounts = hallCounts();
            r.supplyVolts = supplyVolts;
            r.motorAmps = motorAmps();
            return r;
        }

        SQM::Roof::Output lastOutput;
    };

    // A controller and a simulated roof stepped together every 20 ms.
    struct Bench
    {
        SQM::Roof::Controller roof;
        RoofSim sim;
        uint32_t nowMs = 1000;
        uint32_t stepMs = 20;

        explicit Bench(const SQM::Roof::Settings &settings = SQM::Roof::Settings(), float startMm = 0.0f)
            : roof(settings)
        {
            sim.travelMm = static_cast<float>(settings.travelMm);
            sim.positionMm = startMm;
            settleHall();
        }

        // Steps with the driver off until the Hall has settled where the roof is
        // and the dead time after start-up has passed (600 ms).
        void settleHall()
        {
            for (uint32_t waited = 0; waited < 600; waited += stepMs)
                step();
        }

        // One control step: the roof moves under the last output, then the logic steps.
        SQM::Roof::Output step()
        {
            sim.drive(roof.output(), stepMs);
            nowMs += stepMs;
            return roof.tick(sim.readings(nowMs));
        }

        void steps(int count)
        {
            for (int i = 0; i < count; ++i)
                step();
        }

        SQM::Roof::CommandResult command(SQM::Roof::Command command, SQM::Roof::Origin origin = SQM::Roof::Origin::Person)
        {
            return roof.command(command, origin);
        }

        // Steps until `until` is true or `maxMs` passes; true if `until` became true.
        template <typename Until> bool runUntil(Until until, uint32_t maxMs)
        {
            for (uint32_t waited = 0; waited < maxMs; waited += stepMs)
            {
                step();
                if (until(roof))
                    return true;
            }
            return false;
        }

        // Steps until the roof is neither opening nor closing.
        bool runToRest(uint32_t maxMs = 400000)
        {
            return runUntil(
                [](const SQM::Roof::Controller &c)
                {
                    const SQM::Roof::State s = c.status().state;
                    return s != SQM::Roof::State::Opening && s != SQM::Roof::State::Closing;
                },
                maxMs);
        }

        SQM::Roof::State state() const { return roof.status().state; }
    };
} // namespace RoofTest
