#include "CloseOnUnsafe.h"

#include <atomic>
#include <esp_attr.h>

#include "Logger.h"
#include "RoofControl.h"
#include "RoofText.h"
#include "SafetyInputs.h"
#include "SafetyPoller.h"

namespace SQM
{
    namespace
    {
        // What must outlive a restart (a crash, the watchdog, a brown-out): a
        // stand-down, so a person's stop is never overridden because the
        // device restarted, and an attempt or back-off with the time it has
        // run, so a board that keeps restarting in the rain still reaches its
        // time limits and alerts. RTC memory survives a reset, not a power cut.
        struct Kept
        {
            uint32_t magic;
            uint32_t seq; // the newer valid slot wins
            uint8_t phase;
            uint8_t event;
            uint8_t cause;
            uint8_t requested;
            uint32_t unsafeMs;
            uint32_t wallMs;
            uint32_t limitMs;
            uint32_t requestAgeMs;
            uint32_t check;
        };
        // Bump the low byte whenever Kept or the enums it holds change.
        constexpr uint32_t KEPT_MAGIC = 0x5EC00504u;
        // Two slots, written alternately and validated last, so a reset in the
        // middle of a write still leaves the previous record.
        RTC_NOINIT_ATTR Kept kept[2];

        uint32_t checkOf(const Kept &k)
        {
            uint32_t sum = 0x9E3779B9u ^ (k.phase | (k.event << 8) | (k.cause << 16) | (static_cast<uint32_t>(k.requested) << 24));
            for (uint32_t value : {k.seq, k.unsafeMs, k.wallMs, k.limitMs, k.requestAgeMs})
                sum = (sum ^ value) * 16777619u;
            return sum;
        }

        bool valid(const Kept &k)
        {
            return k.magic == KEPT_MAGIC && k.check == checkOf(k);
        }

        // The newest valid slot, or nullptr.
        const Kept *newest()
        {
            const bool a = valid(kept[0]);
            const bool b = valid(kept[1]);
            if (a && b)
                return static_cast<int32_t>(kept[1].seq - kept[0].seq) > 0 ? &kept[1] : &kept[0];
            return a ? &kept[0] : b ? &kept[1] : nullptr;
        }

        uint32_t clampMs(uint64_t ms)
        {
            return ms > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(ms);
        }

        CloseSeq::Delivery deliveryOf(uint32_t id, const CloseOnUnsafe::DeliveryProbe &probe)
        {
            return id == 0 || !probe ? CloseSeq::Delivery::Failed : probe(id);
        }
    } // namespace

    void CloseOnUnsafe::begin(const Config &config)
    {
        updateConfig(config);
        const Kept *k = newest();
        if (k == nullptr)
            return;
        keptSeq = k->seq;
        CloseSeq::Sequence::Saved saved;
        saved.phase = static_cast<CloseSeq::Phase>(k->phase);
        saved.event = static_cast<CloseSeq::Event>(k->event);
        saved.cause = static_cast<CloseSeq::Cause>(k->cause);
        saved.unsafeMs = k->unsafeMs;
        saved.wallMs = k->wallMs;
        saved.limitMs = k->limitMs;
        saved.requested = k->requested != 0;
        saved.requestAgeMs = k->requestAgeMs;
        sequence.restore(saved);
        if (sequence.phase() != CloseSeq::Phase::Idle)
            Logger::warn(
                TAG,
                "Close sequence carries on after the restart (%s, %u s)",
                CloseSeq::phaseName(sequence.phase()),
                static_cast<unsigned>(k->unsafeMs / 1000));
    }

    // Every step: RTC memory is plain RAM, so keeping the running time costs
    // nothing. The other slot is written, then validated, so the previous
    // record stands until the new one is complete.
    void CloseOnUnsafe::keep()
    {
        const CloseSeq::Sequence::Saved saved = sequence.saved();
        if (saved.phase == CloseSeq::Phase::Idle && !saved.requested)
        {
            kept[0].magic = 0;
            kept[1].magic = 0;
            return;
        }
        Kept &slot = kept[(keptSeq + 1) & 1u];
        slot.magic = 0;
        std::atomic_signal_fence(std::memory_order_seq_cst);
        slot.seq = keptSeq + 1;
        slot.phase = static_cast<uint8_t>(saved.phase);
        slot.event = static_cast<uint8_t>(saved.event);
        slot.cause = static_cast<uint8_t>(saved.cause);
        slot.requested = saved.requested ? 1 : 0;
        slot.unsafeMs = clampMs(saved.unsafeMs);
        slot.wallMs = clampMs(saved.wallMs);
        slot.limitMs = clampMs(saved.limitMs);
        slot.requestAgeMs = clampMs(saved.requestAgeMs);
        slot.check = checkOf(slot);
        std::atomic_signal_fence(std::memory_order_seq_cst);
        slot.magic = KEPT_MAGIC;
        keptSeq = slot.seq;
    }

    void CloseOnUnsafe::handle(SafetyInputs &safety, RoofControl &roof)
    {
        // Ten steps a second: well inside every limit of the sequence, and the
        // roof's lock isn't taken on every loop pass.
        const uint64_t now = SafetyPoller::nowMs() + TIME_BASE_MS;
        if (stepped && now - lastStepMs < STEP_EVERY_MS)
            return;
        stepped = true;
        lastStepMs = now;
        stepNowMs = now;
        // One consistent copy of the roof: its state, a waiting close and a
        // person's stops come from the same moment (spec 003's lock).
        const RoofIo::Snapshot snap = roof.snapshot();
        CloseSeq::Inputs in;
        in.nowMs = now;
        CloseSeq::applySafety(in, safety.weather().verdict, safety.park());
        CloseSeq::applyRoof(in, snap.status, snap.closePending);
        in.personStops = snap.personStops;
        in.roofStarted = roof.running();
        sequence.setParkTimeoutMs(parkTimeoutMs.load());

        const CloseSeq::Decision decision = sequence.step(in);
        keep();
        retryAlert(now);
        if (decision.action == CloseSeq::Action::None && decision.event == CloseSeq::Event::None && sequence.phase() == loggedPhase)
            return; // the usual pass: nothing to do

        CloseSeq::Reasons reasons;
        reasons.park = Safety::describe(safety.park());
        reasons.fault = RoofIo::describeFault(snap.fault);
        reasons.roof = snap.status.state;
        const std::string weather = Safety::describe(safety.weather());
        personStopsSeen = snap.personStops;
        const CloseSeq::Decision answer = carryOut(decision, safety, roof, reasons.refusal);
        report(decision, reasons, weather);
        report(answer, reasons, weather);
        keep();
        if (sequence.phase() != loggedPhase)
        {
            Logger::info(TAG, "Close sequence: %s -> %s", CloseSeq::phaseName(loggedPhase), CloseSeq::phaseName(sequence.phase()));
            loggedPhase = sequence.phase();
        }
    }

    // A critical alert no channel took is sent again, at most every minute,
    // until one does or the sequence is idle (constitution III).
    void CloseOnUnsafe::retryAlert(uint64_t nowMs)
    {
        if (sequence.phase() == CloseSeq::Phase::Idle)
            retry.clear();
        if (!retry.active() || nowMs - lastProbeMs < PROBE_EVERY_MS)
            return;
        lastProbeMs = nowMs;
        if (!retry.resendDue(deliveryOf(lastAlertId, deliveryProbe), nowMs))
            return;
        Logger::warn(TAG, "No channel took the alert \"%s\": sending it again", lastAlert.title.c_str());
        lastAlertId = alertSink ? alertSink(lastAlert) : 0;
        retry.resent(nowMs);
    }

    void CloseOnUnsafe::send(const Alerts::Alert &alert, uint64_t nowMs)
    {
        if (!alertSink)
            return;
        lastAlert = alert;
        lastAlertId = alertSink(alert);
        retry.sent(nowMs);
    }

    CloseSeq::Decision CloseOnUnsafe::carryOut(
        const CloseSeq::Decision &decision, SafetyInputs &safety, RoofControl &roof, std::string &refusal)
    {
        switch (decision.action)
        {
        case CloseSeq::Action::RequestPark:
            Logger::info(TAG, "Asking N.I.N.A. to park the mount");
            safety.requestPark();
            break;
        case CloseSeq::Action::Close:
        {
            // Through spec 003's gates: the park at the start, the interlock throughout.
            const RoofControl::Outcome outcome = roof.automaticClose(personStopsSeen);
            refusal = outcome.text;
            if (outcome.accepted)
                Logger::info(TAG, "Closing the roof: the mount is parked");
            return sequence.closeAnswered(CloseSeq::answerFor(outcome.accepted, outcome.reason));
        }
        case CloseSeq::Action::Stop:
            roof.command(Roof::Command::Stop, Roof::Origin::Automatic);
            break;
        case CloseSeq::Action::None:
            break;
        }
        return CloseSeq::Decision{};
    }

    void CloseOnUnsafe::report(const CloseSeq::Decision &decision, const CloseSeq::Reasons &reasons, const std::string &weather)
    {
        if (decision.event == CloseSeq::Event::None)
            return;
        if (!CloseSeq::isCritical(decision.event))
        {
            if (decision.event == CloseSeq::Event::Started)
                Logger::info(TAG, "Close sequence started: %s", weather.c_str());
            else
                Logger::info(TAG, "Close sequence: %s", CloseSeq::eventName(decision.event));
            return;
        }
        const CloseSeq::AlertText text = CloseSeq::alertFor(decision, CloseSeq::reasonFor(decision, reasons));
        Logger::error(TAG, "%s: %s", text.title.c_str(), text.message.c_str());
        if (!decision.alert || !alertSink)
            return; // already alerted in this episode
        Alerts::Alert alert;
        alert.type = Alerts::AlertType::Roof;
        alert.level = Alerts::AlertLevel::Wake;
        alert.title = text.title;
        alert.message = text.message;
        send(alert, stepNowMs);
    }
} // namespace SQM
