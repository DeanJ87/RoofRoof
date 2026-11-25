#include "FlashGuard.h"

#include <Arduino.h>
#include <atomic>

#include "MotionGuards.h"

namespace SQM
{
    namespace FlashGuard
    {
        namespace
        {
            portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
            RoofIo::FlashActivity activity; // guarded by lock
            // Spec 029: the same, readable without the lock (from the panic hook).
            std::atomic<bool> writingMirror{false};

            // Under the lock, after every change.
            void mirror(uint32_t nowMs)
            {
                writingMirror.store(activity.writing() || activity.busy(nowMs), std::memory_order_relaxed);
            }
        } // namespace

        bool writingNow()
        {
            return writingMirror.load(std::memory_order_relaxed);
        }

        uint32_t begin()
        {
            const uint32_t now = millis();
            portENTER_CRITICAL(&lock);
            const uint32_t token = activity.begin(now);
            mirror(millis());
            portEXIT_CRITICAL(&lock);
            return token;
        }

        void progress(uint32_t token)
        {
            const uint32_t now = millis();
            portENTER_CRITICAL(&lock);
            activity.progress(token, now);
            mirror(millis());
            portEXIT_CRITICAL(&lock);
        }

        void end(uint32_t token)
        {
            portENTER_CRITICAL(&lock);
            activity.end(token);
            mirror(millis());
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

        void holdShort()
        {
            portENTER_CRITICAL(&lock);
            activity.holdShort();
            mirror(millis());
            portEXIT_CRITICAL(&lock);
        }

        void releaseShort()
        {
            portENTER_CRITICAL(&lock);
            activity.releaseShort();
            mirror(millis());
            portEXIT_CRITICAL(&lock);
        }

        bool writing()
        {
            portENTER_CRITICAL(&lock);
            const bool result = activity.writing();
            portEXIT_CRITICAL(&lock);
            return result;
        }
    } // namespace FlashGuard
} // namespace SQM
