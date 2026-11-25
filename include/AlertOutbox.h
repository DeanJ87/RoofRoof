#pragma once

// Spec 030 (FR-008): every alert waits in lib/CloseSequenceLogic's AlertQueue
// until a channel takes it, and the critical ones (mount not parked, roof
// didn't close, roof needs a person) are also kept in NVS, one record per
// event, so a restart or a power cut doesn't lose them: sent again after the
// restart, marked as sent before it. Main loop only.

#include <cstdint>

#include "AlertEngine.h"
#include "AlertQueue.h"

namespace SQM
{
    class AlertOutbox
    {
    public:
        // setup(), before the main loop: queues the kept records again.
        void begin();
        // A new alert (critical ones raw, the rest finished for sending).
        // `resolvable`: dropped when its condition ends (resolve()).
        void push(const Alerts::Alert &alert, bool resolvable);
        void resolve(Alerts::AlertType type) { queue.resolve(type); }
        void setMaxGap(uint64_t ms) { queue.setMaxGap(ms); }
        // Every pass: sends what is due (about once a second) and brings the
        // kept records in line with the queue when a flash write may run.
        void handle(bool networkUp, const CloseSeq::AlertQueue::Send &send, const CloseSeq::AlertQueue::Probe &probe);

    private:
        static constexpr const char *TAG = "AlertQueue";
        static constexpr uint32_t STEP_EVERY_MS = 1000;
        static constexpr size_t KEPT_TYPES = 3;
        static const Alerts::AlertType KEPT[KEPT_TYPES];

        void keep();

        CloseSeq::AlertQueue queue;
        uint32_t keptSerial[KEPT_TYPES] = {}; // the push whose record is in NVS (0: none)
        uint32_t queuedAt[KEPT_TYPES] = {};   // epoch seconds the waiting one was queued
        bool stepped = false;
        uint32_t lastStepMs = 0;
    };
} // namespace SQM
