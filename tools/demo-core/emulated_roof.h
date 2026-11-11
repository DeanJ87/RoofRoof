#pragma once

// The demo's roof (spec 011, contracts/demo-roof.md): the firmware's motion
// logic (lib/RoofLogic), command checks and gates (lib/RoofIoLogic), close
// sequence and roof alerts (lib/CloseSequenceLogic) and weather and park rules
// (lib/SafetyInputLogic), driving the simulated roof (lib/RoofPlantLogic)
// instead of the IBT-2. SQMeter and N.I.N.A. are simulated too; the Demo
// panel's injectors decide what they answer. Stepped every 20 ms of device
// time, caught up to the clock the demo hands in, as the control task steps
// the real roof. The glue mirrors src/RoofControl and src/CloseOnUnsafe;
// every decision is the libraries'.

#include <ArduinoJson.h>

#include <cstdint>
#include <string>
#include <vector>

#include "AlertEngine.h"
#include "AlpacaRouter.h"
#include "CloseSequence.h"
#include "Config.h"
#include "Daylight.h"
#include "FaultRecord.h"
#include "InputsReport.h"
#include "RoofController.h"
#include "RoofGates.h"
#include "RoofPlant.h"
#include "RoofStatus.h"
#include "SafetyInput.h"

namespace DemoRoof
{
    using namespace SQM;

    constexpr uint32_t STEP_MS = 20;              // RoofIo::CONTROL_PERIOD_MS
    constexpr uint32_t SEQUENCE_EVERY_MS = 100;   // CloseOnUnsafe::STEP_EVERY_MS
    constexpr uint32_t EVALUATE_EVERY_MS = 1000;  // SafetyInputs' evaluation
    constexpr uint32_t SQMETER_POLL_MS = 5000;    // the simulated SQMeter's HTTP poll
    constexpr uint32_t MQTT_REPUBLISH_MS = 60000; // SQMeter republishes <base>/safe
    constexpr uint32_t NINA_POLL_MS = 2000;       // the N.I.N.A. poll (research D8 of spec 004)
    constexpr uint32_t SLEW_MS = 8000;            // the simulated mount's slew to park
    constexpr uint32_t MAX_CATCH_UP_MS = 600000;  // a longer jump is skipped (a tab asleep)
    constexpr float NORMAL_SUPPLY_VOLTS = 12.6f;
    constexpr float LOW_SUPPLY_VOLTS = 10.5f;
    constexpr uint32_t UNSAFE_FLAG = 1u << 0; // an SQMeter reason that isn't rain

    // The Demo panel's settings.
    struct Injectors
    {
        bool unsafe = false;    // SQMeter: unsafe for a reason other than rain
        bool rain = false;      // SQMeter (and a fitted rain contact): rain
        bool parkFails = false; // the mount never reaches park
        bool jam = false;       // the roof can't move
        bool supplyLow = false; // the 12 V supply at LOW_SUPPLY_VOLTS
        bool sunUp = false;     // spec 013: the sun well above the daylight threshold
    };

    // The simulated mount as N.I.N.A. reports it.
    struct Mount
    {
        bool atPark = true;
        bool slewing = false;
        uint64_t slewEndsMs = 0;
    };

    // A command's answer, as RoofControl::Outcome.
    struct Outcome
    {
        bool accepted = true;
        std::string reason;
        std::string text;
        Roof::State state = Roof::State::Unknown;
    };

    class EmulatedRoof
    {
    public:
        // A (re)start of the device at `nowMs`: a new controller, safety
        // inputs and sequence; the roof stays where it is and a latched fault
        // stays latched (the device's fault store).
        void boot(const Config &cfg, uint64_t nowMs);
        // Saved settings: the motion logic takes them once no move is active.
        void settingsChanged(const Config &cfg);
        // Steps the roof up to `nowMs`.
        void advance(uint64_t nowMs);
        // The device's clock (Unix seconds, 0 when not set), for the daylight state.
        void setClock(int64_t epochSeconds) { epoch = epochSeconds; }

        // POST /api/roof/* (a person) and the Alpaca Dome (a client).
        Outcome command(Roof::Command command, Roof::Origin origin);
        Outcome clearFault();
        Alpaca::DomeStatus domeStatus() const;
        Alpaca::DomeAnswer domeCommand(Alpaca::DomeAction action);

        // The panel.
        const Injectors &injectors() const { return inject; }
        void setInjectors(const Injectors &next);
        // The imaging app parks (a slew) or unparks the mount.
        void parkMount();
        void unparkMount();
        const Mount &mountState() const { return mount; }

        // What the status, the alerts and the panel show.
        RoofIo::Snapshot snapshot() const;
        const Safety::WeatherVerdict &weather() const { return verdict; }
        const Safety::ParkResult &parkResult() const { return park; }
        // Spec 015: GET /api/roof/inputs, as SafetyInputs::report().
        Safety::InputsReport inputs() const;
        CloseSeq::Phase phase() const { return sequence.phase(); }
        const Alerts::RoofInputs &conditions() const { return roofConditions; }
        const Daylight::View &daylight() const { return daylightView; }
        // The critical alerts raised since the last call (spec 005's sink).
        std::vector<Alerts::Alert> takeCritical();

        // sessionStorage: where the roof is, its fault, the panel and the mount.
        void save(JsonObject out) const;
        void load(JsonObjectConst in);

    private:
        void step();
        void stepSafety();
        void evaluateSafety();
        void stepMount();
        void stepSequence();
        void stepDaylight(bool now);
        void carryOut(const CloseSeq::Decision &decision, const CloseSeq::Reasons &reasons);
        void report(const CloseSeq::Decision &decision, const CloseSeq::Reasons &reasons);
        Outcome automaticClose(uint32_t personStopsSeen);
        Outcome runChecked(Roof::Command command, Roof::Origin origin);
        Outcome outcomeOf(const Roof::CommandResult &result) const;
        void checkGates();
        void latchExternal(const RoofIo::FaultRecord &record);
        void syncFault();
        void takePendingSettings();
        void sourcesChanged(const SafetyConfig &nextSafety, const ParkConfig &nextPark);
        Roof::Readings readings();
        Safety::SqmeterReply sqmeterReply() const;
        RoofIo::Contact switchContact() const;

        SafetyConfig safetyCfg;
        ParkConfig parkCfg;
        RoofConfig roofCfg;
        bool mqttOn = false;
        bool settingsPending = false;
        RoofConfig pendingRoof;

        Roof::Controller controller;
        RoofPlant::Plant plant = RoofPlant::forSettings(Roof::Settings(), 0.0f);
        RoofIo::FaultRecord fault;
        RoofIo::GateInputs gates;
        uint32_t personStops = 0;
        uint32_t remoteStops = 0; // spec 014: of personStops, the honoured remote stops
        float motorAmps = 0.0f;

        Safety::SourceRecord http;
        Safety::SourceRecord mqtt;
        Safety::WeatherVerdict verdict;
        Safety::ParkResult park;
        Nina::MountReading ninaReading; // the last evaluation's
        // Bumped when that part's settings change (the poller's and SafetyInputs' generations).
        uint32_t httpGeneration = 0;
        uint32_t mqttGeneration = 0;
        uint32_t ninaGeneration = 0;
        Mount mount;
        Injectors inject;
        bool weatherChanged = false;

        CloseSeq::Sequence sequence;
        Alerts::RoofInputs roofConditions;
        uint32_t closedCount = 0;
        // Spec 013: CloseOnUnsafe::updateDaylight.
        Daylight::Settings daylightCfg;
        CloseSeq::Policy policy;       // spec 014: autoClose.*
        bool remoteStopsClose = false; // spec 014: autoClose.remoteStop
        Daylight::Watch daylightWatch;
        Daylight::View daylightView;
        uint32_t daylightClosedCount = 0;
        int64_t epoch = 0;
        uint64_t lastDaylightMs = 0;
        std::vector<Alerts::Alert> critical;

        uint64_t nowMs = 0; // stepped up to here
        uint64_t bootMs = 0;
        uint64_t lastSqmeterMs = 0;
        uint64_t lastMqttMs = 0;
        uint64_t lastNinaMs = 0;
        uint64_t ninaReadAtMs = 0;
        uint64_t lastEvaluateMs = 0;
        uint64_t lastSequenceMs = 0;
        bool polled = false;
    };
} // namespace DemoRoof
