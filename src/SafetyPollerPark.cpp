#include "Logger.h"
#include "SafetyPoller.h"

// Spec 050 (constitution 12.1.0, II): the park request. A request is decided in
// the poller pass on a N.I.N.A. reading taken after it: never sent while the
// mount slews (N.I.N.A.'s API would stop a park under way), not at all when it
// reads parked. The decisions are lib/SafetyInputLogic's (ParkSend).

namespace SQM
{
    namespace
    {
        constexpr const char *TAG = "Safety";
        constexpr unsigned WAIT_S = Safety::PARK_SLEW_WAIT_MS / 1000;
    } // namespace

    void SafetyPoller::requestPark()
    {
        parkRequested = true;
    }

    SafetyPoller::Snapshot SafetyPoller::snapshot() const
    {
        if (mutex == nullptr)
            return Snapshot{};
        xSemaphoreTake(mutex, portMAX_DELAY);
        Snapshot copy = state;
        copy.parkPending = parkRequested;
        xSemaphoreGive(mutex);
        return copy;
    }

    Safety::ParkSendEvent SafetyPoller::stepPark(const Targets &now, uint32_t generation)
    {
        Safety::ParkSendInputs in;
        in.ninaSet = !now.ninaUrl.empty();
        in.roofAtEnd = mountMayMove.load(); // spec 043, checked again at the send
        xSemaphoreTake(mutex, portMAX_DELAY);
        in.reading = state.nina;
        xSemaphoreGive(mutex);
        in.nowMs = nowMs();
        const Safety::ParkSendEvent event = parkSender.step(in);
        switch (event)
        {
        case Safety::ParkSendEvent::Waiting:
            Logger::info(TAG, "Not asking N.I.N.A. to park yet: the mount is slewing (waiting up to %u s for it to stop)", WAIT_S);
            break;
        case Safety::ParkSendEvent::AlreadyParked:
            Logger::info(TAG, "The mount is already parked: no park request sent");
            break;
        case Safety::ParkSendEvent::Dropped:
            Logger::warn(TAG, "Not asking N.I.N.A. to park: the roof isn't at an end");
            break;
        case Safety::ParkSendEvent::StopSlew:
            Logger::warn(
                TAG,
                "No sign the mount stopped slewing in %u s: asking N.I.N.A. to stop the slew, then to park once the mount is still",
                WAIT_S);
            break;
        case Safety::ParkSendEvent::SendAtLimit:
            Logger::warn(
                TAG,
                "The mount still reads slewing %u s after the stop: asking N.I.N.A. to park anyway",
                static_cast<unsigned>(Safety::PARK_STOP_WAIT_MS / 1000));
            break;
        case Safety::ParkSendEvent::Send:
        case Safety::ParkSendEvent::None:
            break;
        }
        xSemaphoreTake(mutex, portMAX_DELAY);
        if (generation == state.ninaGeneration)
            state.parkWaitingForSlew = parkSender.waitingForSlew();
        xSemaphoreGive(mutex);
        return event;
    }
} // namespace SQM
