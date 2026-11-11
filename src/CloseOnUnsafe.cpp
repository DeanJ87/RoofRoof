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
        // A stand-down that must outlive a restart (a crash, the watchdog, an
        // update): a person's stop is never overridden because the device
        // restarted. RTC memory survives a reset but not a power cut.
        struct Kept
        {
            uint32_t magic;
            uint8_t phase;
            uint8_t event;
            uint8_t cause;
            uint8_t check;
        };
        // Bump the low byte whenever Kept or the enums it holds change.
        constexpr uint32_t KEPT_MAGIC = 0x5EC00501u;
        RTC_NOINIT_ATTR Kept kept;

        uint8_t checkOf(uint8_t phase, uint8_t event, uint8_t cause)
        {
            return static_cast<uint8_t>(phase ^ (event << 3) ^ (cause << 5) ^ 0xA5u);
        }
    } // namespace

    void CloseOnUnsafe::begin(const Config &config)
    {
        updateConfig(config);
        if (kept.magic != KEPT_MAGIC || kept.check != checkOf(kept.phase, kept.event, kept.cause))
            return;
        sequence.restore(
            static_cast<CloseSeq::Phase>(kept.phase), static_cast<CloseSeq::Event>(kept.event), static_cast<CloseSeq::Cause>(kept.cause));
        loggedPhase = sequence.phase();
        if (loggedPhase != CloseSeq::Phase::Idle)
            Logger::warn(TAG, "Close sequence still stood down after the restart (%s)", CloseSeq::phaseName(loggedPhase));
    }

    void CloseOnUnsafe::keepPhase()
    {
        const CloseSeq::Phase phase = sequence.phase();
        if (phase != CloseSeq::Phase::HeldByPerson && phase != CloseSeq::Phase::Halted)
        {
            kept.magic = 0;
            return;
        }
        kept.phase = static_cast<uint8_t>(phase);
        kept.event = static_cast<uint8_t>(sequence.lastEvent());
        kept.cause = static_cast<uint8_t>(sequence.lastCause());
        kept.check = checkOf(kept.phase, kept.event, kept.cause);
        kept.magic = KEPT_MAGIC;
    }

    void CloseOnUnsafe::handle(SafetyInputs &safety, RoofControl &roof)
    {
        // Ten steps a second: well inside every limit of the sequence, and the
        // roof's lock isn't taken on every loop pass.
        const uint64_t now = SafetyPoller::nowMs();
        if (stepped && now - lastStepMs < STEP_EVERY_MS)
            return;
        stepped = true;
        lastStepMs = now;
        // One consistent copy of the roof: its state, a waiting close and a
        // person's stops come from the same moment (spec 003's lock).
        const RoofIo::Snapshot snap = roof.snapshot();
        CloseSeq::Inputs in;
        in.nowMs = now;
        CloseSeq::applySafety(in, safety.weather().verdict, safety.park());
        CloseSeq::applyRoof(in, snap.status, snap.closePending);
        in.personStops = snap.personStops;
        sequence.setParkTimeoutMs(parkTimeoutMs.load());

        const CloseSeq::Decision decision = sequence.step(in);
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
        if (sequence.phase() != loggedPhase)
        {
            Logger::info(TAG, "Close sequence: %s -> %s", CloseSeq::phaseName(loggedPhase), CloseSeq::phaseName(sequence.phase()));
            loggedPhase = sequence.phase();
            keepPhase();
        }
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
        alertSink(alert);
    }
} // namespace SQM
