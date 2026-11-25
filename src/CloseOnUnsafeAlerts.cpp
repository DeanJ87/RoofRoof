#include "CloseOnUnsafe.h"

#include "Logger.h"
#include "SafetyPoller.h"

// Spec 030: the close sequence's alerts through the alert outbox, and the
// needs-a-person watcher (never silent: a restart, a fault, a Hall sensor
// failure). Main loop, after each step of the sequence.

namespace SQM
{
    void CloseOnUnsafe::settleAlerts(const RoofIo::Snapshot &snap, bool roofStarted)
    {
        const CloseSeq::Phase phase = sequence.phase();
        // The sequence is idle: the park and close alerts have nothing left to say.
        if (phase == CloseSeq::Phase::Idle && alertOut.resolve)
        {
            alertOut.resolve(Alerts::AlertType::MountNotParked);
            alertOut.resolve(Alerts::AlertType::RoofNotClosed);
        }
        CloseSeq::NeedsPersonInputs in;
        in.nowMs = SafetyPoller::nowMs();
        in.roof = &snap;
        in.roofStarted = roofStarted;
        in.phase = phase;
        {
            std::lock_guard<std::mutex> held(daylightLock);
            in.remindMs = policy.remindMs;
        }
        Alerts::Alert alert;
        if (needsPerson.step(in, alert))
        {
            Logger::error(TAG, "%s: %s", alert.title.c_str(), alert.message.c_str());
            send(alert, needsPerson.conditionHolds());
        }
        const bool holds = needsPerson.active();
        if (needsPersonHeld && !holds && alertOut.resolve)
            alertOut.resolve(Alerts::AlertType::NeedsPerson);
        if (needsPersonHeld != holds)
            Logger::info(TAG, "Roof needs a person: %s", holds ? needsPerson.text().c_str() : "no longer");
        needsPersonHeld = holds;
        RoofIo::NeedsPersonStatus shown{holds, needsPerson.text()};
        std::lock_guard<std::mutex> held(needsPersonLock);
        needsPersonShown = std::move(shown);
    }

    RoofIo::NeedsPersonStatus CloseOnUnsafe::needsPersonStatus() const
    {
        std::lock_guard<std::mutex> held(needsPersonLock);
        return needsPersonShown;
    }
} // namespace SQM
