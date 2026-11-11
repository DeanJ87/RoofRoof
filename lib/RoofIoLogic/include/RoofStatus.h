#pragma once

// The roof section of /api/status and /ws/status (spec 003 FR-013,
// data-model.md): what the roof is doing, its raw readings and any fault.
// Unreadable values are null, never a made-up number.

#include <ArduinoJson.h>
#include <cstdint>

#include "FaultRecord.h"
#include "RoofTypes.h"
#include "SwitchInput.h"

namespace SQM
{
    namespace RoofIo
    {
        // A consistent copy of everything the section shows, taken under the
        // roof's lock and written outside it.
        struct Snapshot
        {
            Roof::Status status;
            FaultRecord fault; // status.fault with its cause
            uint32_t travelMm = 1850;
            float supplyVolts = 0.0f; // NaN when unreadable
            float minSupplyVolts = 11.0f;
            bool currentSense = false;
            float motorAmps = 0.0f; // NaN when off or unreadable
            float maxCurrentAmps = 10.0f;
            uint8_t fittedSwitches = 0;
            Contact switch1 = Contact::Unreadable;
            Contact switch2 = Contact::Unreadable;
            bool rainFitted = false;
            Contact rain = Contact::Unreadable;
            bool closePending = false; // a close waiting for an upload or MQTT connect to stop
            uint32_t personStops = 0;  // a person's stops and aborts since start (spec 005; not in the document)
            uint32_t watchdogTrips = 0;
            uint32_t maxStepGapMs = 0;
            uint32_t maxWatchdogBeatAgeMs = 0; // the oldest watchdog heartbeat a step saw (limit 50 ms)
        };

        // The supply is unreadable or below its minimum.
        bool supplyLow(const Snapshot &snapshot);

        // Writes the section's keys into `out` (the caller creates the "roof" object).
        void writeStatus(JsonObject out, const Snapshot &snapshot);
    } // namespace RoofIo
} // namespace SQM
