#pragma once

#include "ChunkText.h"

// Spec 038: GET /api/diag/heap, where the byte-addressable heap went
// (HeapSafe::HeapCensus over ESP-IDF's heap walk and FreeRTOS's task list).

namespace SQM
{
    namespace HeapMap
    {
        // The census as text; false (nothing written) when its working memory
        // couldn't be had or another census is running.
        bool text(HeapSafe::ChunkText &out);
        // Main loop, every pass: takes the emergency reserve back once there is
        // room and logs its use (spec 038; the new-handler that spends it can't
        // log), and records the heap's trend once a second (spec 045).
        void upkeep();
    } // namespace HeapMap
} // namespace SQM
