#pragma once

// The roof's states, commands, faults and the values the motion logic takes and
// gives (spec 002, data-model.md). Plain values only: the hardware glue (spec
// 003) fills Readings from the pins and applies Output through lib/DriverLogic.

#include <cstdint>

#include "DriverLogic.h"

namespace SQM
{
    namespace Roof
    {
        // What the roof is doing, as reported (constitution I).
        enum class State : uint8_t
        {
            Unknown, // between the magnets with no estimate: only a person's slow move
            Closed,  // closed magnet read
            Open,    // open magnet read
            Opening, // a move to open, including the dead time before the driver turns on
            Closing, // a move to close
            Stopped, // stopped part-way; the position is the time-based estimate
            Error,   // a fault is latched: no motion until a person clears it
        };

        // One end of the travel.
        enum class End : uint8_t
        {
            Closed,
            Open,
        };

        // What is asked of the roof.
        enum class Command : uint8_t
        {
            Open,
            Close,
            Stop,  // driver off, estimate kept
            Abort, // driver off, position unknown part-way (constitution I)
        };

        // Who asks. Only a person may move a roof whose position is unknown or
        // clear a fault (constitution I).
        enum class Origin : uint8_t
        {
            Person,    // the authenticated web UI
            Automatic, // the controller itself (spec 005's close)
            Client,    // an ASCOM Alpaca client (spec 006): no login, so not a person
            Mqtt,      // a command on the MQTT roof topic (spec 008): no login, so not a person
        };

        // The Hall reading after confirmation (HallFilter).
        enum class HallLevel : uint8_t
        {
            Unsettled, // not yet three equal readings
            Low,       // at or below the low threshold: one end's magnet
            Between,   // no magnet
            High,      // at or above the high threshold: the other end's magnet
            Rail,      // at either end of the ADC range: sensor disconnected or shorted
        };

        // Why the roof stopped and locked out (constitution I's motion faults).
        enum class FaultKind : uint8_t
        {
            None,
            Timeout,           // no target magnet within the move's timeout
            DidNotLeave,       // still on the start magnet after the leave time
            WrongMagnet,       // the other end's magnet, or the start magnet again
            HallRail,          // Hall reading at a rail
            OverCurrent,       // motor current at or above its limit for CONFIRM_MS
            CurrentUnreadable, // current sense on and no reading for CONFIRM_MS
            SupplyLow,         // supply below its minimum, or unreadable, for CONFIRM_MS
            LateStep,          // a control step later than MAX_STEP_GAP_MS while driving
            DriveBudget,       // driven too long in total without reaching a magnet
            External,          // raised by the caller (watchdog, park interlock)
        };

        // A fault and the numbers behind it (constitution III: the measured value and
        // the limit). Units by kind are in data-model.md.
        struct Fault
        {
            FaultKind kind = FaultKind::None;
            float measured = 0.0f;
            float limit = 0.0f;
        };

        // Why a command was refused.
        enum class Refusal : uint8_t
        {
            None,
            FaultLatched,
            HallNotSettled,
            PositionUnknown,   // an automatic move with the position unknown
            SupplyLow,         // measured volts, minimum
            SupplyUnreadable,  // limit: minimum volts
            CurrentUnreadable, // current sense on, no reading; limit: max amps
            SettingsInvalid,
            NotAPerson, // clearing a fault (or a calibration leg) needs a person
            Moving,     // new settings, or a calibration leg, while a move is active
            LegInvalid, // spec 016: a calibration leg outside its ranges
        };

        // The answer to a command.
        struct CommandResult
        {
            bool accepted = true;
            Refusal reason = Refusal::None;
            float measured = 0.0f;
            float limit = 0.0f;
        };

        // One control step's inputs. NaN means the reading couldn't be taken.
        struct Readings
        {
            uint32_t nowMs = 0;      // monotonic, wraps after 49.7 days
            uint16_t hallCounts = 0; // raw 12-bit ADC counts
            float supplyVolts = 0.0f;
            float motorAmps = 0.0f; // ignored while current sense is off
        };

        // What the driver must do now; handed to Driver::outputsFor.
        struct Output
        {
            Driver::Command command = Driver::Command::Off;
            uint8_t duty = 0; // 0-255
        };

        // Everything the logic reports.
        struct Status
        {
            State state = State::Unknown;
            bool positionKnown = false;
            float positionMm = 0.0f;  // 0 closed, travelMm open; only meaningful when known
            End target = End::Closed; // valid while opening or closing
            bool driving = false;     // the driver is on (false during the dead time)
            bool fromUnknown = false; // the move started from an unknown position
            uint32_t moveElapsedMs = 0;
            uint32_t moveTimeoutMs = 0;
            Fault fault;
            HallLevel hall = HallLevel::Unsettled;
            uint16_t hallCounts = 0;
            bool calibrating = false; // spec 016: the active move is a calibration leg
        };

        // Spec 016: one leg of an attended calibration run (Controller::calibrate).
        // A person's move toward `target` at a fixed duty profile, bounded by its
        // own limit, that stops at the first magnet after leaving the one it
        // started on: the ends are told apart by the direction driven, not by
        // the polarity setting (which the run is measuring).
        struct CalibrationLeg
        {
            End target = End::Closed; // drive toward this end (the open direction setting maps it)
            uint8_t dutyPercent = 0;  // the cruise duty: the slow duty up to the top duty
            // Above the slow duty: ramp back down to it after this much driving
            // (over the ramp-down time); 0 never does.
            uint32_t slowAfterMs = 0;
            uint32_t limitMs = 0; // the leg's hard timeout and drive budget
            // Low or High: the leg must end on that level (the other is a wrong
            // magnet); Unsettled: any magnet.
            HallLevel expect = HallLevel::Unsettled;
        };

        // How the last calibration leg ended (Controller::calibrationReport).
        struct CalibrationReport
        {
            uint32_t ended = 0;   // calibration legs ended since construction
            bool arrived = false; // stopped at a magnet; false: stopped, replaced or faulted
            End target = End::Closed;
            HallLevel startLevel = HallLevel::Unsettled; // when the driver turned on
            HallLevel level = HallLevel::Unsettled;      // where it stopped
            uint32_t drivenMs = 0;                       // from the driver on to the stop
            float dutyMs = 0.0f;                         // the duty fraction integrated over drivenMs
        };

        // Stable machine names for status documents, logs and MQTT (never translated).
        const char *stateName(State state);
        const char *endName(End end);
        const char *commandName(Command command);
        const char *faultName(FaultKind kind);
        const char *refusalName(Refusal reason);
        const char *hallLevelName(HallLevel level);
    } // namespace Roof
} // namespace SQM
