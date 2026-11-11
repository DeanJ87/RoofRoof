#include "CloseOnUnsafe.h"

#include <Arduino.h>
#include <atomic>
#include <ctime>
#include <esp_attr.h>

#include "DeviceCore.h"
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
            uint8_t requested; // bit 0: a park request was sent; bit 1: a daylight-only episode (spec 013)
            uint32_t unsafeMs;
            uint32_t wallMs;
            uint32_t limitMs;
            uint32_t requestAgeMs;
            uint32_t check;
        };
        // Bump the low byte whenever Kept or the enums it holds change.
        // Spec 013 added bit 1 of `requested` without changing the layout: a
        // record from an older image (bit 1 always clear) stays valid, so an
        // upgrade keeps a person's stand-down.
        constexpr uint32_t KEPT_MAGIC = 0x5EC00504u;
        constexpr uint8_t KEPT_REQUESTED = 1u << 0;
        constexpr uint8_t KEPT_DAYLIGHT = 1u << 1;
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

    void CloseOnUnsafe::updateConfig(const Config &config)
    {
        parkTimeoutMs.store(config.park.timeoutSeconds * 1000u);
        std::lock_guard<std::mutex> held(daylightLock);
        daylightSettings = Daylight::settingsFrom(config);
    }

    void CloseOnUnsafe::updateDaylight()
    {
        const uint32_t now = millis();
        if (daylightStepped && now - lastDaylightMs < DAYLIGHT_EVERY_MS)
            return;
        daylightStepped = true;
        lastDaylightMs = now;
        Daylight::Settings settings;
        {
            std::lock_guard<std::mutex> held(daylightLock);
            settings = daylightSettings;
        }
        const int64_t epoch = static_cast<int64_t>(time(nullptr));
        const Daylight::View before = daylightView;
        daylightView = daylightWatch.update(settings, epoch >= Core::CLOCK_VALID_EPOCH, epoch);
        if (daylightView.state != before.state || daylightView.mode != before.mode)
            Logger::info(TAG, "Daylight: %s (%s)", Daylight::stateName(daylightView.state), Daylight::describe(daylightView).c_str());
        const RoofIo::DaylightStatus shown = CloseSeq::daylightStatus(daylightView);
        std::lock_guard<std::mutex> held(daylightLock);
        daylightShown = shown;
    }

    RoofIo::DaylightStatus CloseOnUnsafe::daylightStatus() const
    {
        std::lock_guard<std::mutex> held(daylightLock);
        return daylightShown;
    }

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
        saved.requested = (k->requested & KEPT_REQUESTED) != 0;
        saved.daylight = (k->requested & KEPT_DAYLIGHT) != 0;
        saved.requestAgeMs = k->requestAgeMs;
        sequence.restore(saved);
        phaseNow.store(static_cast<uint8_t>(sequence.phase()));
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
        slot.requested = static_cast<uint8_t>((saved.requested ? KEPT_REQUESTED : 0) | (saved.daylight ? KEPT_DAYLIGHT : 0));
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
        in.daylight = daylightView.closes(); // spec 013
        sequence.setParkTimeoutMs(parkTimeoutMs.load());

        const CloseSeq::Phase before = sequence.phase();
        const CloseSeq::Decision decision = sequence.step(in);
        phaseNow.store(static_cast<uint8_t>(sequence.phase()));
        keep();
        retryAlert(now);
        countClosed(before, decision);
        const CloseSeq::SequenceView view{sequence.phase(), closedCount, sequence.daylightEpisode(), daylightClosedCount};
        CloseSeq::updateConditions(roofConditions, snap, safety.weather(), view);
        CloseSeq::updateDaylightConditions(roofConditions, daylightView, snap.status.state, view);
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
        phaseNow.store(static_cast<uint8_t>(sequence.phase()));
        keep();
        if (sequence.phase() != loggedPhase)
        {
            Logger::info(TAG, "Close sequence: %s -> %s", CloseSeq::phaseName(loggedPhase), CloseSeq::phaseName(sequence.phase()));
            loggedPhase = sequence.phase();
        }
    }

    // Each close the sequence saw finish, as a weather or a daylight episode (spec 013).
    void CloseOnUnsafe::countClosed(CloseSeq::Phase before, const CloseSeq::Decision &decision)
    {
        if (!CloseSeq::closedAfterAttempt(before, decision.event))
            return;
        if (decision.daylight)
            ++daylightClosedCount;
        else
            ++closedCount;
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
            if (decision.event == CloseSeq::Event::Started && decision.daylight)
                Logger::info(TAG, "Close sequence started: daylight, %s", Daylight::describe(daylightView).c_str());
            else if (decision.event == CloseSeq::Event::Started)
                Logger::info(TAG, "Close sequence started: %s", weather.c_str());
            else
                Logger::info(TAG, "Close sequence: %s", CloseSeq::eventName(decision.event));
            return;
        }
        const CloseSeq::AlertText text = CloseSeq::alertFor(decision, CloseSeq::reasonFor(decision, reasons));
        Logger::error(TAG, "%s: %s", text.title.c_str(), text.message.c_str());
        if (!decision.alert || !alertSink)
            return; // already alerted in this episode
        // Typed, with its variables; the sink applies the event's settings
        // (spec 007) each time it is sent or sent again.
        send(CloseSeq::criticalAlert(decision, CloseSeq::reasonFor(decision, reasons)), stepNowMs);
    }
} // namespace SQM
