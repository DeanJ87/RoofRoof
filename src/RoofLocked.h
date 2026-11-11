#pragma once

// Holds the roof's mutex for one scope (src/RoofControl*.cpp). Every holder
// only runs the logic and writes pins: nothing waits on the network or the
// flash while holding it.

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace SQM
{
    class RoofLocked
    {
    public:
        explicit RoofLocked(SemaphoreHandle_t mutex)
            : mutex(mutex)
        {
            xSemaphoreTake(mutex, portMAX_DELAY);
        }
        ~RoofLocked() { xSemaphoreGive(mutex); }
        RoofLocked(const RoofLocked &) = delete;
        RoofLocked &operator=(const RoofLocked &) = delete;

    private:
        SemaphoreHandle_t mutex;
    };
} // namespace SQM
