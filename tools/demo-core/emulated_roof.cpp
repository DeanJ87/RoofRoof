// EmulatedRoof: the demo's roof (see emulated_roof.h). The order of each step
// is the firmware's: RoofControl::stepLocked for the roof, SafetyInputs for
// the verdict and park, CloseOnUnsafe::handle for the sequence.

#include "emulated_roof.h"

#include "MotionProfile.h"

#include <cmath>

#include "CommandCheck.h"
#include "DeviceCore.h"
#include "DomeBridge.h"
#include "MotionGuards.h"
#include "ParkRule.h"
#include "RoofAlerts.h"
#include "RoofSettings.h"
#include "RoofText.h"

namespace DemoRoof
{
    namespace
    {
        // CloseOnUnsafe::TIME_BASE_MS: the sequence's clock starts a week in.
        constexpr uint64_t TIME_BASE_MS = 7ull * 24 * 3600 * 1000;
    } // namespace

    // --- Start-up and settings ------------------------------------------------

    void EmulatedRoof::boot(const Config &cfg, uint64_t now)
    {
        safetyCfg = cfg.safety;
        parkCfg = cfg.park;
        roofCfg = cfg.roof;
        mqttOn = cfg.mqtt.enabled;
        daylightCfg = Daylight::settingsFrom(cfg);
        policy = CloseSeq::policyFrom(cfg);
        remoteStopsClose = cfg.autoClose.remoteStop;
        guard = RoofIo::MoveGuard(); // spec 027: a restart ends a wait
        guard.configure(RoofIo::moveGuardSettingsOf(cfg));
        settingsPending = false;
        pendingAnalog = cfg.analog;
        Roof::Settings settings = Roof::settingsFromConfig(roofCfg);
        settings.analogUsable = analogUsable(pendingAnalog, roofCfg); // as RoofControl::motionSettings
        controller = Roof::Controller(settings);
        RoofPlant::applySettings(plant, settings, roofCfg.minRunningAmps);
        // Constitution I: a restart never clears the lockout.
        if (fault.fault.kind != Roof::FaultKind::None)
            controller.raiseFault(fault.fault);
        if (fault.blindDone)
            controller.restoreBlindDone(); // spec 030: one blind close per fault
        gates = RoofIo::GateInputs();
        gates.fittedSwitches = static_cast<uint8_t>(parkCfg.switches);
        personStops = 0;
        remoteStops = 0;
        http = Safety::SourceRecord();
        mqtt = Safety::SourceRecord();
        // As SafetyPoller::configure and SafetyInputs::apply set them: a source
        // switched on is waiting at boot, never "off".
        http.enabled = safetyCfg.httpEnabled;
        mqtt.enabled = safetyCfg.mqttEnabled;
        verdict = Safety::WeatherVerdict();
        park = Safety::ParkResult();
        sequence = CloseSeq::Sequence();
        nothingWantsClose = true;
        weatherHold = CloseSeq::WeatherHold(); // spec 030: a fresh boot starts with the hold empty
        ninaLostWatch = CloseSeq::NinaLostWatch();
        needsPerson = CloseSeq::NeedsPerson();
        roofConditions = Alerts::RoofInputs();
        closedCount = 0;
        daylightClosedCount = 0;
        daylightWatch = Daylight::Watch();
        daylightView = Daylight::View();
        critical.clear();
        nowMs = now;
        bootMs = now;
        lastEvaluateMs = lastSequenceMs = now;
        polled = false;
        ninaReadAtMs = 0;
        evaluateSafety();
        stepDaylight(true);
    }

    void EmulatedRoof::settingsChanged(const Config &cfg)
    {
        daylightCfg = Daylight::settingsFrom(cfg);
        policy = CloseSeq::policyFrom(cfg);
        remoteStopsClose = cfg.autoClose.remoteStop;
        guard.configure(RoofIo::moveGuardSettingsOf(cfg)); // spec 027: applies to the next wait
        stepDaylight(true);
        sourcesChanged(cfg.safety, cfg.park);
        safetyCfg = cfg.safety;
        parkCfg = cfg.park;
        testBoxCfg = cfg.testBox;
        mqttOn = cfg.mqtt.enabled;
        pendingRoof = cfg.roof;
        pendingAnalog = cfg.analog;
        settingsPending = true;
        takePendingSettings();
    }

    // SafetyPoller::configure and SafetyInputs::apply: a changed source starts
    // again from nothing under a new generation and is polled at once.
    void EmulatedRoof::sourcesChanged(const SafetyConfig &nextSafety, const ParkConfig &nextPark)
    {
        bool repoll = false;
        if (nextSafety.httpEnabled != safetyCfg.httpEnabled || nextSafety.httpUrl != safetyCfg.httpUrl ||
            nextSafety.httpUsername != safetyCfg.httpUsername || nextSafety.httpPassword != safetyCfg.httpPassword)
        {
            http = Safety::SourceRecord();
            http.enabled = nextSafety.httpEnabled;
            ++httpGeneration;
            repoll = true;
        }
        if (nextSafety.mqttEnabled != safetyCfg.mqttEnabled || nextSafety.mqttTopic != safetyCfg.mqttTopic)
        {
            mqtt = Safety::SourceRecord();
            mqtt.enabled = nextSafety.mqttEnabled;
            ++mqttGeneration;
            repoll = true;
        }
        if (nextPark.ninaUrl != parkCfg.ninaUrl)
        {
            ++ninaGeneration;
            ninaReadAtMs = 0;
            repoll = true;
        }
        if (repoll)
            polled = false;
    }

    // RoofControl::takePendingSettings: refused while a move is active, kept until it ends.
    void EmulatedRoof::takePendingSettings()
    {
        if (!settingsPending)
            return;
        Roof::Settings settings = Roof::settingsFromConfig(pendingRoof);
        settings.analogUsable = analogUsable(pendingAnalog, pendingRoof); // as RoofControl::motionSettings
        if (!controller.applySettings(settings).accepted)
            return;
        roofCfg = pendingRoof;
        settingsPending = false;
        gates.fittedSwitches = static_cast<uint8_t>(parkCfg.switches);
        RoofPlant::applySettings(plant, settings, roofCfg.minRunningAmps); // the simulated roof is what the settings describe
    }

    // --- Stepping ---------------------------------------------------------------

    void EmulatedRoof::advance(uint64_t now)
    {
        if (now < nowMs)
            return;
        if (now - nowMs > MAX_CATCH_UP_MS)
            nowMs = now - MAX_CATCH_UP_MS;
        while (nowMs + STEP_MS <= now)
        {
            nowMs += STEP_MS;
            step();
        }
    }

    // One control step (RoofControl::stepLocked), then the main loop's work
    // that falls due (the safety inputs, the close sequence).
    void EmulatedRoof::step()
    {
        const Roof::Readings in = readings();
        takePendingSettings();
        gates.switch1 = gates.switch2 = switchContact();
        watchSwitches();
        checkGates();
        controller.tick(in);
        syncFault();
        stepCalibration(in);
        teach.step(controller.status(), in.hallCounts); // spec 019: the reading window
        if (diagLog != nullptr)
            recordDiag(in); // spec 017: after the output, as RoofControl::stepLocked
        stepMount();
        stepSafety();
        stepDaylight(false);
        if (nowMs - lastSequenceMs >= SEQUENCE_EVERY_MS)
        {
            lastSequenceMs = nowMs;
            stepSequence();
        }
    }

    // The roof moved under the last output for one step; what the ADC would read now.
    Roof::Readings EmulatedRoof::readings()
    {
        plant.supplyVolts = inject.supplyLow ? LOW_SUPPLY_VOLTS : NORMAL_SUPPLY_VOLTS;
        plant.jammed = inject.jam;
        plant.drive(controller.output(), STEP_MS);
        Roof::Readings r = plant.readings(static_cast<uint32_t>(nowMs));
        const float sense = roofCfg.currentSenseEnabled ? plant.senseAmps() : NAN;
        r.motorAmps = RoofIo::runningAmps(sense, controller.status(), controller.output(), roofCfg.minRunningAmps);
        motorAmps = r.motorAmps;
        return r;
    }

    // RoofControl::checkGates: spec 027's wait for the park and the overrides
    // in effect, then constitution II's interlock, III's stop of an open.
    void EmulatedRoof::checkGates()
    {
        stepGuard();
        const Roof::Status &status = controller.status();
        if (RoofIo::interlockLost(
                status, gates, guard.closeParkOverride(), guard.closeNinaLost(), guard.openParkOverride())) // specs 030, 039
            latchExternal(
                RoofIo::externalRecord(RoofIo::FaultCause::ParkInterlock, static_cast<float>(RoofIo::interlockBits(gates)), 0.0f));
        else if (RoofIo::openMustStop(status, gates, guard.openOverride()))
            controller.command(Roof::Command::Stop, Roof::Origin::Automatic);
    }

    // RoofControl::stepCalibration: after the tick (the demo has no blockers).
    void EmulatedRoof::stepCalibration(const Roof::Readings &in)
    {
        if (!calibration.active())
            return;
        Calib::StepInputs step;
        step.readings = in;
        step.gates = gates;
        step.faultText = RoofIo::describeFault(fault);
        calibration.step(controller, step);
        syncFault();
    }

    Outcome EmulatedRoof::startCalibration(const Calib::Request &request)
    {
        Calib::StartInputs in;
        in.nowMs = static_cast<uint32_t>(nowMs);
        in.limitMs = roofCfg.calibrationLimitSeconds * 1000;
        in.sequenceIdle = CloseSeq::platformState(sequence.phase()) == RoofIo::SequenceState::Idle;
        in.settingsPending = settingsPending;
        in.gates = gates;
        in.faultText = RoofIo::describeFault(fault);
        const Calib::Answer answer = calibration.start(controller, request, in);
        return Outcome{answer.accepted, answer.reason, answer.text, controller.status().state};
    }

    void EmulatedRoof::latchExternal(const RoofIo::FaultRecord &record)
    {
        const bool wasClear = controller.status().fault.kind == Roof::FaultKind::None;
        controller.raiseFault(record.fault);
        if (wasClear)
            fault = record;
        syncFault();
    }

    void EmulatedRoof::syncFault()
    {
        const Roof::Fault &now = controller.status().fault;
        if (now.kind == Roof::FaultKind::None)
            fault = RoofIo::FaultRecord();
        else if (fault.fault.kind == Roof::FaultKind::None)
            fault = RoofIo::FaultRecord{now, RoofIo::FaultCause::Logic};
        if (now.kind != Roof::FaultKind::None)
            fault.blindDone = controller.blindDone(); // spec 030: saved with the fault
    }

    // --- The simulated SQMeter and N.I.N.A. -------------------------------------

    Safety::SqmeterReply EmulatedRoof::sqmeterReply() const
    {
        Safety::SqmeterReply reply;
        reply.reasonFlags = (inject.unsafe ? UNSAFE_FLAG : 0) | (inject.rain ? Safety::SQMETER_RAIN : 0);
        reply.safe = reply.reasonFlags == 0;
        return reply;
    }

    // Fitted park switches close when the mount is at park.
    RoofIo::Contact EmulatedRoof::switchContact() const
    {
        return mount.atPark && !mount.slewing ? RoofIo::Contact::Closed : RoofIo::Contact::Open;
    }

    void EmulatedRoof::stepMount()
    {
        if (mount.slewing && nowMs >= mount.slewEndsMs)
        {
            mount.slewing = false;
            mount.atPark = !inject.parkFails;
        }
    }

    void EmulatedRoof::parkMount()
    {
        if (mount.atPark || mount.slewing)
            return;
        mount.slewing = true;
        mount.slewEndsMs = nowMs + SLEW_MS;
    }

    void EmulatedRoof::unparkMount()
    {
        mount.atPark = false;
        mount.slewing = false;
    }

    // The polls fall due, then the verdict and park are evaluated once a second.
    void EmulatedRoof::stepSafety()
    {
        http.enabled = safetyCfg.httpEnabled;
        mqtt.enabled = safetyCfg.mqttEnabled;
        const bool first = !polled;
        polled = true;
        if (http.enabled && (first || nowMs - lastSqmeterMs >= SQMETER_POLL_MS))
        {
            lastSqmeterMs = nowMs;
            pollSqmeterHttp(); // spec 020: offline or locked answers as a real SQMeter would
        }
        // SQMeter publishes on a change, and again every minute (retained); nothing while offline.
        if (mqtt.enabled && mqttOn && !inject.sqmeterOffline && (first || weatherChanged || nowMs - lastMqttMs >= MQTT_REPUBLISH_MS))
        {
            lastMqttMs = nowMs;
            mqtt.recordGood(sqmeterReply(), nowMs);
        }
        weatherChanged = false;
        if (!parkCfg.ninaUrl.empty() && (first || nowMs - lastNinaMs >= NINA_POLL_MS))
        {
            lastNinaMs = nowMs;
            ninaReadAtMs = nowMs;
        }
        if (nowMs - lastEvaluateMs >= EVALUATE_EVERY_MS)
        {
            lastEvaluateMs = nowMs;
            evaluateSafety();
        }
    }

    // SafetyInputs::evaluate, then RoofControl::setSafety.
    void EmulatedRoof::evaluateSafety()
    {
        Safety::WeatherInputs in;
        in.http = http;
        in.mqtt = mqtt;
        in.httpLimitMs = safetyCfg.staleSeconds * 1000;
        in.startedAtMs = bootMs;
        in.nowMs = nowMs;
        verdict = Safety::evaluateWeather(in);

        Safety::ParkInputs p;
        if (parkCfg.ninaUrl.empty())
            p.nina.status = Nina::ReadingStatus::NotConfigured;
        else if (polled && ninaReadAtMs != 0)
        {
            p.nina.status = Nina::ReadingStatus::Good;
            p.nina.connected = true;
            p.nina.atPark = mount.atPark && !mount.slewing;
            p.nina.slewing = mount.slewing;
            p.nina.takenAtMs = ninaReadAtMs;
        }
        p.fittedSwitches = static_cast<uint8_t>(parkCfg.switches);
        p.switchesOnly = parkCfg.switchesOnly(); // spec 024
        p.switch1 = p.switch2 = RoofIo::parkReading(switchContact());
        p.nowMs = nowMs;
        park = Safety::evaluatePark(p);
        ninaReading = p.nina;
        // Spec 022: the test box's bypasses, as SafetyInputs::evaluate.
        testBoxState = TestBox::evaluate(testBoxCfg, TestBox::sourcesOf(safetyCfg, parkCfg), epoch >= Core::CLOCK_VALID_EPOCH, epoch);
        Safety::CloseHold hold;
        TestBox::applyWeather(verdict, testBoxState);
        TestBox::applyPark(park, hold, testBoxState);

        gates.weather = verdict.verdict;
        gates.weatherText = Safety::describe(verdict);
        gates.weatherReasons = verdict.reasons; // spec 027: what a person may override
        gates.parked = park.parked;
        gates.parkText = Safety::describe(park);
    }

    Safety::InputsReport EmulatedRoof::inputs() const
    {
        return Safety::InputsReport{verdict, park, ninaReading, nowMs, httpGeneration, mqttGeneration, ninaGeneration};
    }

    // --- Daylight (CloseOnUnsafe::updateDaylight and main.cpp's hand-over) -----------

    void EmulatedRoof::stepDaylight(bool now)
    {
        if (!now && nowMs - lastDaylightMs < EVALUATE_EVERY_MS)
            return;
        lastDaylightMs = nowMs;
        Daylight::Settings settings = daylightCfg;
        settings.testBox = testBoxState.daylight; // spec 022
        daylightView = daylightWatch.update(settings, epoch >= Core::CLOCK_VALID_EPOCH, epoch, inject.sunUp);
        gates.daylightCloses = daylightView.closes();
        gates.daylightText = gates.daylightCloses ? Daylight::describe(daylightView) : std::string();
    }

    // --- The close sequence (CloseOnUnsafe::handle) ------------------------------

    const char *EmulatedRoof::releaseHold()
    {
        // As CloseOnUnsafe::requestRelease: the weather as the sequence last saw it (spec 030: through the hold).
        const char *refusal = CloseSeq::releaseRefusal(sequence.phase(), nothingWantsClose);
        if (refusal == nullptr)
            releaseWanted = true;
        return refusal;
    }

    void EmulatedRoof::stepSequence()
    {
        CloseSeq::Inputs in;
        in.nowMs = nowMs + TIME_BASE_MS;
        CloseSeq::applySafety(in, verdict.verdict, park);
        CloseSeq::applyRoof(in, controller.status(), false);
        in.personStops = personStops;
        in.remoteStops = remoteStops;
        in.roofStarted = true;
        in.daylight = daylightView.closes();
        CloseSeq::applyOverride(in, overrideBits(), verdict); // spec 027
        holdWeather(in);                                      // spec 030
        watchNina(in);                                        // spec 030
        in.releaseHold = releaseWanted;                       // spec 026
        releaseWanted = false;
        sequence.setParkTimeoutMs(parkCfg.timeoutSeconds * 1000);
        sequence.setPolicy(policy);
        const CloseSeq::Phase before = sequence.phase();
        const CloseSeq::Decision decision = sequence.step(in);
        nothingWantsClose = in.weatherSafe && !in.daylight;
        if (CloseSeq::closedAfterAttempt(before, decision.event))
            ++(decision.daylight ? daylightClosedCount : closedCount);
        settleAlerts(); // spec 030
        const CloseSeq::SequenceView view{sequence.phase(), closedCount, sequence.daylightEpisode(), daylightClosedCount};
        CloseSeq::updateConditions(roofConditions, snapshot(), verdict, view);
        CloseSeq::updateDaylightConditions(roofConditions, daylightView, controller.status().state, view);
        CloseSeq::updateNinaConditions(roofConditions, park, !parkCfg.ninaUrl.empty()); // spec 027
        roofConditions.testBox = testBoxState.bits();                                   // spec 022
        roofConditions.testBoxText = roofConditions.testBox != 0 ? TestBox::describe(testBoxState) : std::string();
        roofConditions.testBoxSettled = !testBoxState.waitingForClock;
        if (decision.action == CloseSeq::Action::None && decision.event == CloseSeq::Event::None)
            return;
        CloseSeq::Reasons reasons;
        reasons.park = CloseSeq::parkReason(park, policy.requestPark);
        reasons.parkGap = CloseSeq::parkGapOf(park); // spec 030
        reasons.fault = RoofIo::describeFault(fault);
        reasons.roof = controller.status().state;
        carryOut(decision, reasons);
    }

    void EmulatedRoof::carryOut(const CloseSeq::Decision &decision, const CloseSeq::Reasons &given)
    {
        switch (decision.action)
        {
        case CloseSeq::Action::RequestPark:
            if (!parkCfg.ninaUrl.empty())
                parkMount();
            break;
        case CloseSeq::Action::Close:
        case CloseSeq::Action::BlindClose:
        {
            // As CloseOnUnsafe::handle: the decision and the roof's answer are
            // reported once the refusal (if any) is known. Spec 030: N.I.N.A.'s
            // part of the park set aside by the N.I.N.A.-lost close.
            const uint8_t bits = decision.ninaLost ? RoofIo::OverrideNinaLost : 0;
            const Outcome outcome = decision.action == CloseSeq::Action::BlindClose ? automaticBlindClose(personStops, bits)
                                                                                    : automaticClose(personStops, bits);
            CloseSeq::Reasons reasons = given;
            reasons.refusal = outcome.text;
            const CloseSeq::Decision answer = sequence.closeAnswered(CloseSeq::answerFor(outcome.accepted, outcome.reason));
            report(decision, reasons);
            report(answer, reasons);
            return;
        }
        case CloseSeq::Action::Stop:
            command(Roof::Command::Stop, Roof::Origin::Automatic);
            break;
        case CloseSeq::Action::None:
            break;
        }
        report(decision, given);
    }

    // A critical event goes to the alert sink once per episode (decision.alert).
    void EmulatedRoof::report(const CloseSeq::Decision &given, const CloseSeq::Reasons &reasons)
    {
        if (!CloseSeq::isCritical(given.event) || !given.alert)
            return;
        CloseSeq::Decision decision = given;
        decision.parkGap = reasons.parkGap; // spec 030: the title names the missing input
        critical.push_back(CloseSeq::criticalAlert(decision, CloseSeq::reasonFor(decision, reasons)));
    }

    // Spec 030 (CloseOnUnsafe::holdWeather): the sequence sees the weather
    // through the hold; the verdict itself, Open's refusal and an opening
    // roof's stop are not delayed.
    void EmulatedRoof::holdWeather(CloseSeq::Inputs &in)
    {
        CloseSeq::WeatherHold::Step step;
        step.nowMs = in.nowMs;
        step.weatherSafe = in.weatherSafe;
        step.rain = (verdict.reasons & Safety::ReasonRain) != 0;
        step.overrideActive = (overrideBits() & RoofIo::OverrideWeather) != 0;
        step.holdMs = policy.unsafeHoldMs;
        in.weatherSafe = weatherHold.update(step);
    }

    // Spec 030 (CloseOnUnsafe::watchNina, autoClose.ninaLostClose).
    void EmulatedRoof::watchNina(CloseSeq::Inputs &in)
    {
        CloseSeq::NinaLostWatch::Step step;
        step.nowMs = in.nowMs;
        step.parkReasons = park.reasons;
        step.enabled = policy.ninaLostClose;
        step.afterMs = policy.ninaLostMs;
        in.ninaLost = ninaLostWatch.update(step) && RoofIo::switchesClosed(gates.fittedSwitches, gates.switch1, gates.switch2);
    }

    // Spec 030 (CloseOnUnsafe::settleAlerts): the needs-a-person watcher. The
    // demo has no outages, so its alerts go straight to the critical sink.
    void EmulatedRoof::settleAlerts()
    {
        const RoofIo::Snapshot snap = snapshot();
        CloseSeq::NeedsPersonInputs in;
        in.nowMs = nowMs - bootMs;
        in.roof = &snap;
        in.roofStarted = true;
        in.phase = sequence.phase();
        in.remindMs = policy.remindMs;
        Alerts::Alert alert;
        if (needsPerson.step(in, alert))
            critical.push_back(alert);
    }

    std::vector<Alerts::Alert> EmulatedRoof::takeCritical()
    {
        std::vector<Alerts::Alert> out;
        out.swap(critical);
        return out;
    }

    // --- The panel and what is shown ----------------------------------------------

    void EmulatedRoof::setInjectors(const Injectors &next)
    {
        weatherChanged = next.unsafe != inject.unsafe || next.rain != inject.rain;
        const bool sunChanged = next.sunUp != inject.sunUp;
        inject = next;
        if (sunChanged)
            stepDaylight(true);
    }

    // --- sessionStorage --------------------------------------------------------------

    void EmulatedRoof::save(JsonObject out) const
    {
        out["positionMm"] = plant.positionMm;
        out["atPark"] = mount.slewing ? !inject.parkFails : mount.atPark; // a slew in progress finishes at once
        JsonObject i = out.createNestedObject("inject");
        i["unsafe"] = inject.unsafe;
        i["rain"] = inject.rain;
        i["parkFails"] = inject.parkFails;
        i["jam"] = inject.jam;
        i["supplyLow"] = inject.supplyLow;
        i["sunUp"] = inject.sunUp;
        if (fault.fault.kind == Roof::FaultKind::None)
            return;
        uint8_t bytes[RoofIo::FAULT_RECORD_BYTES];
        RoofIo::encodeFault(fault, bytes);
        JsonArray f = out.createNestedArray("fault");
        for (uint8_t b : bytes)
            f.add(b);
    }

    // Before boot(): the roof where it was, its fault as the fault store keeps it.
    void EmulatedRoof::load(JsonObjectConst in)
    {
        plant.positionMm = in["positionMm"] | 0.0f;
        mount = Mount();
        mount.atPark = in["atPark"] | true;
        JsonObjectConst i = in["inject"];
        inject.unsafe = i["unsafe"] | false;
        inject.rain = i["rain"] | false;
        inject.parkFails = i["parkFails"] | false;
        inject.jam = i["jam"] | false;
        inject.supplyLow = i["supplyLow"] | false;
        inject.sunUp = i["sunUp"] | false;
        fault = RoofIo::FaultRecord();
        JsonArrayConst f = in["fault"];
        if (f.isNull())
            return;
        uint8_t bytes[RoofIo::FAULT_RECORD_BYTES] = {};
        size_t n = 0;
        for (JsonVariantConst b : f)
            if (n < sizeof(bytes))
                bytes[n++] = b.as<uint8_t>();
        RoofIo::decodeFault(bytes, n, fault); // unreadable latches storedUnreadable, as on the device
    }
} // namespace DemoRoof
