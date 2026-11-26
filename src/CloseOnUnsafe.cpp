#include "CloseOnUnsafe.h"

#include <Arduino.h>
#include <algorithm>
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

    } // namespace

    void CloseOnUnsafe::updateConfig(const Config &config)
    {
        parkTimeoutMs.store(config.park.timeoutSeconds * 1000u);
        std::lock_guard<std::mutex> held(daylightLock);
        daylightSettings = Daylight::settingsFrom(config);
        policy = CloseSeq::policyFrom(config);
    }

    void CloseOnUnsafe::updateDaylight(bool testBox)
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
        settings.testBox = testBox;
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
        if (sequence.phase() == CloseSeq::Phase::Idle)
            return;
        // Spec 030: an attempt restored mid-way was already wanted: no new hold.
        weatherHold.startFired();
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
        in.remoteStops = snap.remoteStops;
        in.roofStarted = roof.running();
        in.daylight = daylightView.closes();                              // spec 013
        CloseSeq::applyOverride(in, snap.overrideBits, safety.weather()); // spec 027
        holdWeather(in, snap.overrideBits, safety.weather().reasons);     // spec 030
        watchNina(in, snap, safety.park());                               // spec 030
        in.releaseHold = releaseWanted.exchange(false);                   // spec 026
        sequence.setParkTimeoutMs(parkTimeoutMs.load());
        {
            std::lock_guard<std::mutex> held(daylightLock);
            sequence.setPolicy(policy);
            // Spec 005: resends up to the reminder interval, and never further apart than its 30 minutes.
            if (alertOut.maxGap)
                alertOut.maxGap(std::min<uint64_t>(policy.remindMs, CloseSeq::ALERT_RETRY_MAX_MS));
        }

        const CloseSeq::Phase before = sequence.phase();
        const CloseSeq::Decision decision = sequence.step(in);
        phaseNow.store(static_cast<uint8_t>(sequence.phase()));
        nothingWantsClose.store(in.weatherSafe && !in.daylight);
        shareStandDown();
        logHoldEnd(before, in);
        keep();
        settleAlerts(snap, in.roofStarted); // spec 030
        countClosed(before, decision);
        updateConditions(snap, safety);
        if (decision.action == CloseSeq::Action::None && decision.event == CloseSeq::Event::None && sequence.phase() == loggedPhase)
            return; // the usual pass: nothing to do

        CloseSeq::Reasons reasons;
        reasons.park = CloseSeq::parkReason(safety.park(), policy.requestPark);
        reasons.parkGap = CloseSeq::parkGapOf(safety.park()); // spec 030
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

    void CloseOnUnsafe::shareStandDown()
    {
        const uint64_t leftMs = sequence.standDownLeftMs();
        standDownLeftS.store(leftMs == CloseSeq::NOT_COUNTING ? -1 : static_cast<int32_t>((leftMs + 999) / 1000));
        standDownLapsing.store(sequence.standDownLapses()); // spec 044
    }

    // Spec 030: the sequence sees the weather through the hold (the verdict
    // itself, Open's refusal and an opening roof's stop are not delayed).
    // Daylight is left as it is.
    void CloseOnUnsafe::holdWeather(CloseSeq::Inputs &in, uint8_t overrideBits, uint32_t reasons)
    {
        CloseSeq::WeatherHold::Step step;
        step.nowMs = in.nowMs;
        step.weatherSafe = in.weatherSafe;
        step.atOnce = CloseSeq::skipsHold(reasons);
        step.overrideActive = (overrideBits & RoofIo::OverrideWeather) != 0;
        {
            std::lock_guard<std::mutex> held(daylightLock);
            step.holdMs = policy.unsafeHoldMs;
        }
        in.raining = CloseSeq::rainForLapse(reasons); // spec 044: what autoClose.personStopLapse "rain" counts
        const bool wasFired = weatherHold.fired();
        in.weatherSafe = weatherHold.update(step);
        if (weatherHold.fired() != wasFired)
            Logger::info(TAG, "%s", weatherHold.fired() ? "Weather hold: the automatic close is wanted" : "Weather hold: clear");
        RoofIo::WeatherHoldStatus shown;
        shown.countedSeconds = static_cast<int32_t>(std::min<uint64_t>(weatherHold.countedMs() / 1000, INT32_MAX));
        const uint64_t left = weatherHold.leftMs();
        shown.leftSeconds = left == CloseSeq::WeatherHold::NOT_HOLDING ? -1 : static_cast<int32_t>((left + 999) / 1000);
        shown.fired = weatherHold.fired();
        std::lock_guard<std::mutex> held(daylightLock);
        holdShown = shown;
    }

    // Spec 030 (autoClose.ninaLostClose): N.I.N.A. lost long enough, and every
    // fitted park switch reads closed (the gate checks them again at the start
    // and throughout the close).
    void CloseOnUnsafe::watchNina(CloseSeq::Inputs &in, const RoofIo::Snapshot &snap, const Safety::ParkResult &park)
    {
        CloseSeq::NinaLostWatch::Step step;
        step.nowMs = in.nowMs;
        step.parkReasons = park.reasons;
        {
            std::lock_guard<std::mutex> held(daylightLock);
            step.enabled = policy.ninaLostClose;
            step.afterMs = policy.ninaLostMs;
        }
        in.ninaLost = ninaLostWatch.update(step) && RoofIo::switchesClosed(snap.fittedSwitches, snap.switch1, snap.switch2);
    }

    RoofIo::WeatherHoldStatus CloseOnUnsafe::weatherHoldStatus() const
    {
        std::lock_guard<std::mutex> held(daylightLock);
        return holdShown;
    }

    // What the roof's non-critical alerts look at (spec 007), after each step.
    void CloseOnUnsafe::updateConditions(const RoofIo::Snapshot &snap, SafetyInputs &safety)
    {
        const CloseSeq::SequenceView view{sequence.phase(), closedCount, sequence.daylightEpisode(), daylightClosedCount};
        CloseSeq::updateConditions(roofConditions, snap, safety.weather(), view);
        CloseSeq::updateDaylightConditions(roofConditions, daylightView, snap.status.state, view);
        CloseSeq::updateNinaConditions(roofConditions, safety.park(), safety.ninaSet()); // spec 027
        // Spec 022: the test box's alert (each bypass coming into effect).
        roofConditions.testBox = safety.testBox().bits();
        roofConditions.testBoxText = roofConditions.testBox != 0 ? TestBox::describe(safety.testBox()) : std::string();
        roofConditions.testBoxSettled = !safety.testBox().waitingForClock;
    }

    // Spec 026's release, and (owner decision 2026-11-24) an Open that ended a stand-down.
    void CloseOnUnsafe::logHoldEnd(CloseSeq::Phase before, const CloseSeq::Inputs &in) const
    {
        if (in.releaseHold)
            Logger::warn(TAG, "%s", CloseSeq::releaseLogText(before, sequence.phase()));
        else if (CloseSeq::openEndedHold(before, sequence.phase(), in.roof))
            Logger::info(TAG, "The roof is opening: the automatic close is armed again");
    }

    const char *CloseOnUnsafe::requestRelease()
    {
        const char *refusal = CloseSeq::releaseRefusal(phase(), nothingWantsClose.load());
        if (refusal == nullptr)
            releaseWanted.store(true);
        return refusal;
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

    void CloseOnUnsafe::parkWaitFailed(const std::string &text)
    {
        Logger::error(TAG, "Mount not parked: %s", text.c_str());
        send(CloseSeq::parkWaitAlert(text), false); // a person's command gave up: sent until a channel takes it
    }

    void CloseOnUnsafe::parkOverridden(const std::string &text)
    {
        send(CloseSeq::parkOverrideAlert(text), false); // it happened, whatever the roof does next
    }

    // Spec 030: through the alert outbox, sent again until a channel takes it.
    void CloseOnUnsafe::send(const Alerts::Alert &alert, bool resolvable)
    {
        if (alertOut.queue)
            alertOut.queue(alert, resolvable);
    }

    CloseSeq::Decision CloseOnUnsafe::carryOut(
        const CloseSeq::Decision &decision, SafetyInputs &safety, RoofControl &roof, std::string &refusal)
    {
        switch (decision.action)
        {
        case CloseSeq::Action::RequestPark:
            // Spec 024: with the park switches alone and no address, only the wait.
            if (safety.ninaSet())
            {
                Logger::info(TAG, "Asking N.I.N.A. to park the mount");
                safety.requestPark();
            }
            break;
        case CloseSeq::Action::Close:
        case CloseSeq::Action::BlindClose:
        {
            // Through spec 003's gates: the park at the start, the interlock
            // throughout (spec 030: N.I.N.A.'s part set aside by the N.I.N.A.-lost close).
            const uint8_t bits = decision.ninaLost ? RoofIo::OverrideNinaLost : 0;
            const RoofControl::Outcome outcome = decision.action == CloseSeq::Action::BlindClose
                                                     ? roof.automaticBlindClose(personStopsSeen, bits)
                                                     : roof.automaticClose(personStopsSeen, bits);
            refusal = outcome.text;
            if (outcome.accepted)
                Logger::info(
                    TAG,
                    "%s",
                    decision.ninaLost ? "Closing the roof: N.I.N.A. lost, its park set aside" : "Closing the roof: the mount is parked");
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

    void CloseOnUnsafe::report(const CloseSeq::Decision &given, const CloseSeq::Reasons &reasons, const std::string &weather)
    {
        if (given.event == CloseSeq::Event::None)
            return;
        CloseSeq::Decision decision = given;
        decision.parkGap = reasons.parkGap; // spec 030: the title names the missing input
        if (decision.event == CloseSeq::Event::HoldLapsed)
        {
            // Spec 044: the roof is about to move by itself after a person's stop.
            const Alerts::Alert alert = CloseSeq::holdLapsedAlert(decision, weather);
            Logger::warn(TAG, "%s: %s", alert.title.c_str(), alert.message.c_str());
            send(alert, false); // it happened: kept until a channel takes it
            return;
        }
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
        if (!decision.alert || !alertOut.queue)
            return; // already alerted in this episode
        // Typed, with its variables; the sink applies the event's settings
        // (spec 007) each time it is sent or sent again.
        // Spec 030: a condition's alert is dropped once the sequence is idle (or
        // the roof no longer needs a person); a report of what happened is kept.
        const CloseSeq::Event e = decision.event;
        const bool condition = e == CloseSeq::Event::ParkNotConfirmed || e == CloseSeq::Event::CloseFailed ||
                               e == CloseSeq::Event::CannotClose || e == CloseSeq::Event::StoppedByPerson;
        send(CloseSeq::criticalAlert(decision, CloseSeq::reasonFor(decision, reasons)), condition);
    }
} // namespace SQM
