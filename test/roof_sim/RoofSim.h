#pragma once

// The native tests' simulated roof (spec 002): spec 011 moved it into
// lib/RoofPlantLogic (RoofPlant::Plant) so the demo and bench mode share it.
// Bench steps a controller and the plant together.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "DriverLogic.h"
#include "RoofController.h"
#include "RoofPlant.h"
#include "RoofTypes.h"

namespace RoofTest
{
    using SQM::Driver::Command;
    using RoofSim = SQM::RoofPlant::Plant;

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
