#pragma once

// GET /api/roof/inputs (spec 015, contracts/roof-inputs.md): the weather
// verdict with each SQMeter source as assessed, the park result and N.I.N.A.'s
// last reading, as the main loop last evaluated them. A copy of what the
// device already holds: building or writing it sends nothing and changes
// nothing. No URL, topic, credential or secret goes into it.

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>

#include "NinaApi.h"
#include "ParkRule.h"
#include "SafetyInput.h"

namespace SQM
{
    namespace Safety
    {
        struct InputsReport
        {
            WeatherVerdict weather;
            ParkResult park;
            Nina::MountReading nina;
            TimeMs nowMs = 0; // when it was taken: N.I.N.A.'s age is counted to here
            // Bumped when that part's settings change (its record starts again).
            uint32_t httpGeneration = 0;
            uint32_t mqttGeneration = 0;
            uint32_t ninaGeneration = 0;
        };

        // Enough for the document with every text at its longest (test_inputs_report).
        constexpr size_t INPUTS_REPORT_CAPACITY = 3072;

        void writeInputsReport(JsonObject out, const InputsReport &report);
        // "good", "stale", ... (the document's names).
        const char *sourceStateName(SourceState state);
        const char *rainContactName(RainContact contact);
        const char *ninaStateName(Nina::ReadingStatus status);
    } // namespace Safety
} // namespace SQM
