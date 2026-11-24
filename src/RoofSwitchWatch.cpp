#include "FlashGuard.h"
#include "Logger.h"
#include "RoofControl.h"
#include "RoofFaultStore.h"
#include "RoofLocked.h"
#include "SwitchWatchStep.h"
#include <Arduino.h>

// Spec 024 (constitution 5.0.0, Principle II): the park switches' stuck-switch
// watch, counted in the control step and kept in NVS beside the roof's fault.
// The decisions are lib/SafetyInputLogic's (SwitchWatch).

namespace SQM
{
    // Spec 024: the step's rule is RoofIo::stepSwitchWatch's.
    void RoofControl::watchSwitches()
    {
        const Roof::State state = controller.status().state;
        gates.stuckSwitch = RoofIo::stepSwitchWatch(
            watch,
            RoofIo::WatchStep{
                watchedState, state, calibration.active(), settings.fittedSwitches, settings.switchesOnly, gates.switch1, gates.switch2});
        watchedState = state;
    }

    // Spec 024: like the fault, written only while the roof is still, at most
    // a couple of times a night (an opening counted, a switch seen open).
    void RoofControl::persistWatch()
    {
        static constexpr uint32_t RETRY_AFTER_MS = 5000;
        const uint32_t now = millis();
        if (watchSaveFailedAtMs != 0 && now - watchSaveFailedAtMs < RETRY_AFTER_MS)
            return;
        Safety::SwitchWatch wanted;
        {
            RoofLocked held(lock);
            if (watch == savedWatch || controller.status().driving || RoofIo::roofMoving(controller.status().state) || closePending)
                return;
            if (blockers && blockers().restartCommitted)
                return;
            wanted = watch;
            FlashGuard::holdShort();
        }
        const bool saved = RoofFaultStore::saveWatch(wanted);
        FlashGuard::releaseShort();
        if (!saved)
        {
            // Retried, so a restart can't take a stuck switch back to a lower count.
            watchSaveFailedAtMs = now == 0 ? 1 : now;
            Logger::error(TAG, "Couldn't save the park switches' watch in NVS; retrying in 5 s");
            return;
        }
        watchSaveFailedAtMs = 0;
        RoofLocked held(lock);
        savedWatch = wanted;
    }
} // namespace SQM
