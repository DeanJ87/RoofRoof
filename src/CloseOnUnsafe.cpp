#include "CloseOnUnsafe.h"

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
        constexpr uint32_t KEPT_MAGIC = 0x5EC00503u;
        RTC_NOINIT_ATTR Kept kept;

        uint32_t checkOf(const Kept &k)
        {
            uint32_t sum = 0x9E3779B9u ^ (k.phase | (k.event << 8) | (k.cause << 16) | (static_cast<uint32_t>(k.requested) << 24));
            for (uint32_t value : {k.unsafeMs, k.wallMs, k.limitMs, k.requestAgeMs})
                sum = (sum ^ value) * 16777619u;
            return sum;
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
        if (kept.magic != KEPT_MAGIC || kept.check != checkOf(kept))
            return;
        CloseSeq::Sequence::Saved saved;
        saved.phase = static_cast<CloseSeq::Phase>(kept.phase);
        saved.event = static_cast<CloseSeq::Event>(kept.event);
        saved.cause = static_cast<CloseSeq::Cause>(kept.cause);
        saved.unsafeMs = kept.unsafeMs;
        saved.wallMs = kept.wallMs;
        saved.limitMs = kept.limitMs;
        saved.requested = kept.requested != 0;
        saved.requestAgeMs = kept.requestAgeMs;
        sequence.restore(saved);
        loggedPhase = sequence.phase();
        if (loggedPhase != CloseSeq::Phase::Idle)
            Logger::warn(
                TAG,
                "Close sequence carries on after the restart (%s, %u s)",
                CloseSeq::phaseName(loggedPhase),
                static_cast<unsigned>(kept.unsafeMs / 1000));
    }

    // Every step: RTC memory is plain RAM, so keeping the running time costs nothing.
    void CloseOnUnsafe::keep()
    {
        const CloseSeq::Sequence::Saved saved = sequence.saved();
        if (saved.phase == CloseSeq::Phase::Idle && !saved.requested)
        {
            kept.magic = 0;
            return;
        }
        kept.magic = 0; // invalid while it changes
        kept.phase = static_cast<uint8_t>(saved.phase);
        kept.event = static_cast<uint8_t>(saved.event);
        kept.cause = static_cast<uint8_t>(saved.cause);
        kept.requested = saved.requested ? 1 : 0;
        kept.requestAgeMs = clampMs(saved.requestAgeMs);
        kept.unsafeMs = clampMs(saved.unsafeMs);
        kept.wallMs = clampMs(saved.wallMs);
        kept.limitMs = clampMs(saved.limitMs);
        kept.check = checkOf(kept);
        kept.magic = KEPT_MAGIC;
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
        send(lastAlert, nowMs);
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
