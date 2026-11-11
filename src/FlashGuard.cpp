#include "FlashGuard.h"

#include <Arduino.h>

#include "MotionGuards.h"

namespace SQM
{
    namespace FlashGuard
    {
        namespace
        {
            portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
            RoofIo::FlashActivity activity; // guarded by lock
        } // namespace

        uint32_t begin()
        {
            const uint32_t now = millis();
            portENTER_CRITICAL(&lock);
            const uint32_t token = activity.begin(now);
            portEXIT_CRITICAL(&lock);
            return token;
        }

        void progress(uint32_t token)
        {
            const uint32_t now = millis();
            portENTER_CRITICAL(&lock);
            activity.progress(token, now);
            portEXIT_CRITICAL(&lock);
        }

        void end(uint32_t token)
        {
            portENTER_CRITICAL(&lock);
            activity.end(token);
            portEXIT_CRITICAL(&lock);
        }

        bool busy()
        {
            const uint32_t now = millis();
            portENTER_CRITICAL(&lock);
            const bool result = activity.busy(now);
            portEXIT_CRITICAL(&lock);
            return result;
        }

        void requestStop()
        {
            portENTER_CRITICAL(&lock);
            activity.requestStop();
            portEXIT_CRITICAL(&lock);
        }

        bool shouldStop(uint32_t token)
        {
            portENTER_CRITICAL(&lock);
            const bool result = activity.shouldStop(token);
            portEXIT_CRITICAL(&lock);
            return result;
        }
    } // namespace FlashGuard
} // namespace SQM
