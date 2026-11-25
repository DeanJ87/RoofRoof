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
    } // namespace HeapMap
} // namespace SQM
