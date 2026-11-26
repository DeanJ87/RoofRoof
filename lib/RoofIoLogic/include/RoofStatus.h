#pragma once

// The roof section of /api/status and /ws/status (spec 003 FR-013,
// data-model.md): what the roof is doing, its raw readings and any fault.
// Unreadable values are null, never a made-up number.

#include <ArduinoJson.h>
#include <cstdint>
#include <string>

#include "FaultRecord.h"
#include "InputsReport.h"
#include "ParkWait.h"
#include "RoofGates.h"
#include "RoofTypes.h"
#include "SwitchInput.h"
#include "TestBox.h"

namespace SQM
{
    namespace RoofIo
    {
        // The daylight failsafe's state (spec 013, Daylight::View), filled in by
        // the status writer's caller like the close phase.
        struct DaylightStatus
        {
            const char *mode = "alert";    // Daylight::modeName
            const char *state = "unknown"; // Daylight::stateName
            bool known = false;            // altitudeDeg holds a value
            double altitudeDeg = 0.0;
            float limitDeg = 0.0f;
            std::string text; // Daylight::describe
            bool closes = false;
        };

        // Spec 027: a command waiting for the park, or how the last wait ended.
        struct WaitView
        {
            bool waiting = false;
            Roof::Command command = Roof::Command::Close;
            Roof::Origin origin = Roof::Origin::Person;
            uint32_t leftMs = 0;
            bool asked = false;
            bool failed = false; // the last wait gave up or was refused (Alpaca: error until the next command)
            std::string text;    // why the last wait ended
        };
        WaitView waitView(const ParkWait &wait, uint32_t nowMs);

        // Spec 027: how N.I.N.A. and SQMeter are answering (from the safety
        // inputs' report), for the status, Home Assistant and the dashboard.
        struct SourcesHealth
        {
            bool ninaSet = false;
            const char *ninaState = "notConfigured"; // Safety::ninaStateName
            bool ninaReachable = false;              // it answered (good, or an answer that says why not)
            bool ninaConnected = false;              // the mount is connected (a good reading)
            bool ninaAtPark = false;
            uint32_t ninaAgeMs = 0; // since the latest reading
            std::string ninaText;   // why it isn't good
            bool sqmeterOn = false;
            const char *sqmeterState = "off"; // Safety::sourceStateName (HTTP first, else MQTT)
            bool sqmeterReplied = false;      // a good reply since the source was switched on
            uint32_t sqmeterAgeMs = 0;        // since that reply
            std::string sqmeterText;          // the latest failure: no reply, HTTP status, unreadable
        };
        SourcesHealth healthOf(const Safety::InputsReport &report, bool ninaSet);

        // Spec 030: the automatic close's weather hold (CloseSeq::WeatherHold):
        // the unsafe seconds added up, the seconds left before the close is
        // wanted (-1: not counting) and whether it has fired.
        struct WeatherHoldStatus
        {
            int32_t countedSeconds = 0;
            int32_t leftSeconds = -1;
            bool fired = false;
        };

        // Spec 030: the roof needs a person (CloseSeq::NeedsPerson), and what for.
        struct NeedsPersonStatus
        {
            bool active = false;
            std::string text;
        };

        // A consistent copy of everything the section shows, taken under the
        // roof's lock and written outside it.
        // Spec 026/044: a stand-down's countdown as the close sequence shares it
        // (CloseOnUnsafe::standDown): the seconds left (-1: not counting), and
        // whether they count down to a person's stop lapsing.
        struct CloseRearm
        {
            int32_t seconds = -1;
            bool lapse = false;
        };

        struct Snapshot
        {
            Roof::Status status;
            FaultRecord fault; // status.fault with its cause
            uint32_t travelMm = 1850;
            float supplyVolts = 0.0f; // NaN when unreadable
            float minSupplyVolts = 11.0f;
            bool currentSense = false;
            std::string pinSetupText; // spec 023: the fault's text while the pins in effect are invalid
            float motorAmps = 0.0f;   // NaN when off or unreadable
            // Spec 046 (MQTT telemetry): the PWM duty applied now (0-100) and the
            // origin of the last accepted open or close. Not in the documents.
            uint8_t dutyPercent = 0;
            Roof::Origin origin = Roof::Origin::Person;
            float maxCurrentAmps = 10.0f;
            uint8_t fittedSwitches = 0;
            Contact switch1 = Contact::Unreadable;
            Contact switch2 = Contact::Unreadable;
            // Spec 024: the switches are the only park source; each switch's
            // roof openings since it last read open; the switch Open is refused for.
            bool switchesOnly = false;
            uint8_t openings[2] = {0, 0};
            uint8_t stuckSwitch = 0;
            bool closePending = false; // a close waiting for an upload or MQTT connect to stop
            bool calibrating = false;  // spec 016: a calibration run is under way (pauses included)
            uint32_t remoteStops = 0;  // spec 014: of personStops, the remote stops honoured by autoClose.remoteStop
            uint32_t personStops = 0;  // a person's stops and aborts since start (spec 005; never an Alpaca client's; not in the document)
            uint32_t watchdogTrips = 0;
            uint32_t maxStepGapMs = 0;
            uint32_t maxWatchdogBeatAgeMs = 0; // the oldest watchdog heartbeat a step saw (limit 50 ms)
            // The weather and park as the command checks see them now (spec 008;
            // applyChecks), and the automatic close's phase (CloseSeq::phaseName,
            // filled in by the status writer's caller).
            Safety::Verdict weather = Safety::Verdict::Unknown;
            std::string weatherText;
            bool parkConfirmed = false;
            std::string parkText;
            const char *closePhase = "idle";
            // Spec 026: a stand-down's seconds left if nothing wants the roof
            // closed meanwhile (CloseSeq::Sequence::standDownLeftMs); -1: not counting (null).
            int32_t closeRearmSeconds = -1;
            // Spec 044: those seconds count down to a person's stop lapsing, after
            // which the close goes ahead (CloseSeq::Sequence::standDownLapses).
            bool closeRearmLapse = false;
            WeatherHoldStatus weatherHold; // spec 030, filled in by the caller
            NeedsPersonStatus needsPerson; // spec 030, filled in by the caller
            // Spec 011: "off" (the real roof), "bench" (bench firmware: simulated
            // roof, motor never driven) or "demo" (the browser demo).
            const char *simulation = "off";
            DaylightStatus daylight;
            // Spec 022: the test box's bypasses as the checks apply them now,
            // filled in by the caller like the daylight.
            TestBox::State testBox;
            // Spec 027: the wait for the park, a person's override in effect
            // (OverrideBit; an Open's or a confirmed close's) and the Open
            // refusals a person may override now.
            WaitView parkWait;
            uint8_t overrideBits = 0;
            uint8_t openOverridable = 0;
            SourcesHealth health; // filled in by the caller, like the daylight
        };

        // The weather and park texts in the documents are cut to this many bytes
        // (with "..." at the end): they can carry N.I.N.A.'s and SQMeter's own
        // error details, and the status and MQTT documents have fixed pools.
        constexpr size_t MAX_CHECK_TEXT = 200;

        // Fills the snapshot's weather and park from the gates the checks use
        // (RoofIo::currentGates): the verdict and its reason; whether a close
        // would pass the park gate now, and why (constitution III's wording).
        void applyChecks(Snapshot &snapshot, const GateInputs &gates);

        // The supply is unreadable or below its minimum.
        bool supplyLow(const Snapshot &snapshot);

        // Spec 022: the roof object's "testBox" (in the status and the MQTT
        // state document; contracts/test-box.md).
        void writeTestBox(JsonObject out, const TestBox::State &state);

        // Writes the section's keys into `out` (the caller creates the "roof" object).
        void writeStatus(JsonObject out, const Snapshot &snapshot);
        // Spec 013: the roof object's "daylight" (the status document only;
        // the MQTT state document leaves it out).
        void writeDaylight(JsonObject out, const Snapshot &snapshot);
        // Spec 027: "parkWait", "parkWaitEnded", "override" and "openOverridable"
        // (the status and the MQTT state document).
        void writeMoveGuard(JsonObject out, const Snapshot &snapshot);
        // Spec 027: "nina" and "sqmeter" (`texts`: with their failure texts,
        // the status document; the MQTT state document leaves them out).
        void writeHealth(JsonObject out, const Snapshot &snapshot, bool texts);
        // Spec 027: why the roof's next move (Open when not open, else Close)
        // can't start now, in a line ("" when nothing blocks it): Home Assistant.
        std::string blockedText(const Snapshot &snapshot);
        // Spec 027: the MQTT state document's "closeRearmSeconds",
        // "daylightState" and "blocked" for Home Assistant.
        void writeDashboard(JsonObject out, const Snapshot &snapshot);
        // Spec 026: the roof object's "closeRearmSeconds" (spec 044: and
        // "closeRearmLapse"), and spec 030's
        // "weatherHold" and "needsPerson.text" (the status document only).
        void writeCloseRearm(JsonObject out, const Snapshot &snapshot);
    } // namespace RoofIo
} // namespace SQM
