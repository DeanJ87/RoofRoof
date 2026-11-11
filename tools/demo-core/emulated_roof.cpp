// EmulatedRoof: the demo's roof (see emulated_roof.h). The order of each step
// is the firmware's: RoofControl::stepLocked for the roof, SafetyInputs for
// the verdict and park, CloseOnUnsafe::handle for the sequence.

#include "emulated_roof.h"

#include <cmath>

#include "CommandCheck.h"
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
        settingsPending = false;
        const Roof::Settings settings = Roof::settingsFromConfig(roofCfg);
        controller = Roof::Controller(settings);
        RoofPlant::applySettings(plant, settings);
        // Constitution I: a restart never clears the lockout.
        if (fault.fault.kind != Roof::FaultKind::None)
            controller.raiseFault(fault.fault);
        gates = RoofIo::GateInputs();
        gates.fittedSwitches = static_cast<uint8_t>(parkCfg.switches);
        personStops = 0;
        http = Safety::SourceRecord();
        mqtt = Safety::SourceRecord();
        verdict = Safety::WeatherVerdict();
        park = Safety::ParkResult();
        sequence = CloseSeq::Sequence();
        roofConditions = Alerts::RoofInputs();
        closedCount = 0;
        critical.clear();
        nowMs = now;
        bootMs = now;
        lastEvaluateMs = lastSequenceMs = now;
        polled = false;
        ninaReadAtMs = 0;
        evaluateSafety();
    }

    void EmulatedRoof::settingsChanged(const Config &cfg)
    {
        safetyCfg = cfg.safety;
        parkCfg = cfg.park;
        mqttOn = cfg.mqtt.enabled;
        pendingRoof = cfg.roof;
        settingsPending = true;
        takePendingSettings();
    }

    // RoofControl::takePendingSettings: refused while a move is active, kept until it ends.
    void EmulatedRoof::takePendingSettings()
    {
        if (!settingsPending)
            return;
        const Roof::Settings settings = Roof::settingsFromConfig(pendingRoof);
        if (!controller.applySettings(settings).accepted)
            return;
        roofCfg = pendingRoof;
        settingsPending = false;
        gates.fittedSwitches = static_cast<uint8_t>(parkCfg.switches);
        RoofPlant::applySettings(plant, settings); // the simulated roof is what the settings describe
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
        checkGates();
        controller.tick(in);
        syncFault();
        stepMount();
        stepSafety();
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

    // RoofControl::checkGates: constitution II's interlock, III's stop of an open.
    void EmulatedRoof::checkGates()
    {
        const Roof::Status &status = controller.status();
        if (RoofIo::interlockLost(status, gates))
            latchExternal(
                RoofIo::externalRecord(RoofIo::FaultCause::ParkInterlock, static_cast<float>(RoofIo::interlockBits(gates)), 0.0f));
        else if (RoofIo::openMustStop(status, gates))
            controller.command(Roof::Command::Stop, Roof::Origin::Automatic);
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
            http.recordGood(sqmeterReply(), nowMs);
        }
        // SQMeter publishes on a change, and again every minute (retained).
        if (mqtt.enabled && mqttOn && (first || weatherChanged || nowMs - lastMqttMs >= MQTT_REPUBLISH_MS))
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
        if (safetyCfg.rainContact)
            in.rainContact = inject.rain ? Safety::RainContact::Raining : Safety::RainContact::Dry;
        in.httpLimitMs = safetyCfg.staleSeconds * 1000;
        in.startedAtMs = bootMs;
        in.nowMs = nowMs;
        verdict = Safety::evaluateWeather(in);

        Safety::ParkInputs p;
        if (parkCfg.ninaUrl.empty())
            p.nina.status = Nina::ReadingStatus::NotConfigured;
        else if (polled)
        {
            p.nina.status = Nina::ReadingStatus::Good;
            p.nina.connected = true;
            p.nina.atPark = mount.atPark && !mount.slewing;
            p.nina.slewing = mount.slewing;
            p.nina.takenAtMs = ninaReadAtMs;
        }
        p.fittedSwitches = static_cast<uint8_t>(parkCfg.switches);
        p.switch1 = p.switch2 = RoofIo::parkReading(switchContact());
        p.nowMs = nowMs;
        park = Safety::evaluatePark(p);

        gates.weather = verdict.verdict;
        gates.weatherText = Safety::describe(verdict);
        gates.parked = park.parked;
        gates.parkText = Safety::describe(park);
    }

    // --- The close sequence (CloseOnUnsafe::handle) ------------------------------

    void EmulatedRoof::stepSequence()
    {
        CloseSeq::Inputs in;
        in.nowMs = nowMs + TIME_BASE_MS;
        CloseSeq::applySafety(in, verdict.verdict, park);
        CloseSeq::applyRoof(in, controller.status(), false);
        in.personStops = personStops;
        in.roofStarted = true;
        sequence.setParkTimeoutMs(parkCfg.timeoutSeconds * 1000);
        const CloseSeq::Phase before = sequence.phase();
        const CloseSeq::Decision decision = sequence.step(in);
        if (CloseSeq::closedAfterAttempt(before, decision.event))
            ++closedCount;
        CloseSeq::updateConditions(roofConditions, snapshot(), verdict, {sequence.phase(), closedCount});
        if (decision.action == CloseSeq::Action::None && decision.event == CloseSeq::Event::None)
            return;
        CloseSeq::Reasons reasons;
        reasons.park = Safety::describe(park);
        reasons.fault = RoofIo::describeFault(fault);
        reasons.roof = controller.status().state;
        report(decision, reasons);
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
        {
            const Outcome outcome = automaticClose(personStops);
            CloseSeq::Reasons reasons = given;
            reasons.refusal = outcome.text;
            report(sequence.closeAnswered(CloseSeq::answerFor(outcome.accepted, outcome.reason)), reasons);
            break;
        }
        case CloseSeq::Action::Stop:
            command(Roof::Command::Stop, Roof::Origin::Automatic);
            break;
        case CloseSeq::Action::None:
            break;
        }
    }

    // A critical event goes to the alert sink once per episode (decision.alert).
    void EmulatedRoof::report(const CloseSeq::Decision &decision, const CloseSeq::Reasons &reasons)
    {
        if (!CloseSeq::isCritical(decision.event) || !decision.alert)
            return;
        critical.push_back(CloseSeq::criticalAlert(decision, CloseSeq::reasonFor(decision, reasons)));
    }

    std::vector<Alerts::Alert> EmulatedRoof::takeCritical()
    {
        std::vector<Alerts::Alert> out;
        out.swap(critical);
        return out;
    }

    // --- Commands -----------------------------------------------------------------

    Outcome EmulatedRoof::outcomeOf(const Roof::CommandResult &result) const
    {
        Outcome outcome;
        outcome.accepted = result.accepted;
        if (!result.accepted)
        {
            outcome.reason = Roof::refusalName(result.reason);
            outcome.text = RoofIo::describeRefusal(result);
        }
        outcome.state = controller.status().state;
        return outcome;
    }

    // RoofControl's precheck and runLocked: the gates, the client rule and the
    // motion logic's own checks, then the command.
    Outcome EmulatedRoof::runChecked(Roof::Command command, Roof::Origin origin)
    {
        const RoofIo::CommandCheck check = RoofIo::checkCommand(command, origin, gates, controller, RoofIo::CommandContext());
        if (!check.accepted)
            return Outcome{false, check.reason, check.text, controller.status().state};
        const bool stopping = command == Roof::Command::Stop || command == Roof::Command::Abort;
        if (stopping && RoofIo::countsAsPersonStop(command, origin))
            personStops++; // the close sequence stands down on a person's stop (spec 005)
        const Outcome outcome = outcomeOf(controller.command(command, origin));
        syncFault();
        return outcome;
    }

    Outcome EmulatedRoof::command(Roof::Command command, Roof::Origin origin)
    {
        return runChecked(command, origin);
    }

    // RoofControl::automaticClose: withdrawn if a person stopped the roof since the decision.
    Outcome EmulatedRoof::automaticClose(uint32_t personStopsSeen)
    {
        if (personStops != personStopsSeen)
            return Outcome{
                false,
                CloseSeq::PERSON_STOP_REASON,
                "A person stopped the roof: the automatic close is withdrawn",
                controller.status().state};
        return runChecked(Roof::Command::Close, Roof::Origin::Automatic);
    }

    Outcome EmulatedRoof::clearFault()
    {
        const Outcome outcome = outcomeOf(controller.clearFault(Roof::Origin::Person));
        syncFault();
        return outcome;
    }

    Alpaca::DomeStatus EmulatedRoof::domeStatus() const
    {
        return RoofIo::domeStatus(controller.status(), false, true);
    }

    // WebServer::AlpacaBackend::domeCommand: the client origin, the same checks.
    Alpaca::DomeAnswer EmulatedRoof::domeCommand(Alpaca::DomeAction action)
    {
        const RoofIo::DomeDispatch dispatch = RoofIo::domeDispatch(action);
        const Outcome outcome = command(dispatch.command, dispatch.origin);
        return RoofIo::domeAnswer(dispatch.command, outcome.accepted, outcome.reason, outcome.text, fault);
    }

    // --- The panel and what is shown ----------------------------------------------

    void EmulatedRoof::setInjectors(const Injectors &next)
    {
        weatherChanged = next.unsafe != inject.unsafe || next.rain != inject.rain;
        inject = next;
    }

    RoofIo::Snapshot EmulatedRoof::snapshot() const
    {
        RoofIo::Snapshot s;
        s.status = controller.status();
        s.fault = fault;
        s.travelMm = roofCfg.travelMm;
        s.supplyVolts = plant.supplyNow();
        s.minSupplyVolts = roofCfg.minSupplyVolts;
        s.currentSense = roofCfg.currentSenseEnabled;
        s.motorAmps = motorAmps;
        s.maxCurrentAmps = roofCfg.maxCurrentAmps;
        s.fittedSwitches = gates.fittedSwitches;
        s.switch1 = gates.switch1;
        s.switch2 = gates.switch2;
        s.rainFitted = safetyCfg.rainContact;
        s.rain = inject.rain ? RoofIo::Contact::Closed : RoofIo::Contact::Open;
        s.personStops = personStops;
        s.maxStepGapMs = STEP_MS;
        s.simulation = "demo";
        RoofIo::applyChecks(s, gates); // spec 008: the weather and park as the checks see them
        s.closePhase = CloseSeq::phaseName(sequence.phase());
        return s;
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
