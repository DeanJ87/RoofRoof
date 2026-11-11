#pragma once

// Spec 016 (contracts/calibration-api.md): GET /api/roof/calibration, written
// from a copy of the run's report.

#include <ArduinoJson.h>
#include <cstddef>
#include <cstdint>

#include "CalibrationRun.h"

namespace SQM
{
    namespace Calib
    {
        // Enough for the fullest document (four legs, every warning, long texts).
        constexpr size_t DOCUMENT_CAPACITY = 4096;

        void writeDocument(JsonObject out, const Report &report, uint32_t nowMs);
    } // namespace Calib
} // namespace SQM
