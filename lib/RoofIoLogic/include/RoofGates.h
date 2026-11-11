#pragma once

// The park and weather checks every roof command passes before it reaches the
// motion logic (spec 003 FR-017 to FR-020, research R8): constitution II
// (close only onto a confirmed park, re-checked throughout the close) and III
// (no opening unless the weather is safe). Spec 005's automatic close goes
// through the same checks. Spec 013: no opening while daylight wants the roof
// closed (daylight mode "alert and close" and the sun up).

#include <cstdint>
#include <string>

#include "RoofTypes.h"
#include "SafetyInput.h"
#include "SwitchInput.h"

namespace SQM
{
    namespace RoofIo
    {
        // What the checks look at: spec 004's verdict and park result (from the
        // main loop, once a second) and the control task's own switch readings
        // (every step).
        struct GateInputs
        {
            Safety::Verdict weather = Safety::Verdict::Unknown;
            std::string weatherText; // Safety::describe(verdict)
            bool parked = false;     // Safety::ParkResult::parked
            std::string parkText;    // Safety::describe(park result)
            uint8_t fittedSwitches = 0;
            Contact switch1 = Contact::Unreadable;
            Contact switch2 = Contact::Unreadable;
            // Spec 013: daylight wants the roof closed (Daylight::View::closes())
            // and why ("Sun 3.2° ≥ 0.0°"), handed over with the verdict.
            bool daylightCloses = false;
            std::string daylightText;
        };

        // The main loop hands the verdict and park result over every 250 ms. If
        // it hasn't for this long (a stalled loop), they count as not safe and
        // not parked: the checks never run on old news. Above SafetyInputs' 1 s
        // evaluation and a blocking MQTT reconnect on the loop, below
        // N.I.N.A.'s 10 s freshness limit.
        constexpr uint32_t SAFETY_HANDOVER_STALE_MS = 5000;

        enum class GateRefusal : uint8_t
        {
            None,
            WeatherNotSafe,
            MountNotParked,
            Daylight, // spec 013: the daylight close wants the roof closed
        };

        // Bits of the park-interlock fault's measured value.
        enum InterlockBit : uint32_t
        {
            InterlockParkResult = 1u << 0, // spec 004's park result is not "parked"
            InterlockSwitch1 = 1u << 1,    // fitted switch 1 doesn't read closed
            InterlockSwitch2 = 1u << 2,    // fitted switch 2 doesn't read closed
        };

        // `in` as the checks may use it: unchanged when the verdict and park
        // result were handed over within SAFETY_HANDOVER_STALE_MS, else with
        // the weather Unknown and the park not confirmed, saying why.
        GateInputs currentGates(const GateInputs &in, bool handedOver, uint32_t ageMs);

        // Every fitted switch reads closed (true when none is fitted).
        bool switchesClosed(uint8_t fitted, Contact switch1, Contact switch2);
        // The park result says parked AND every fitted switch reads closed now.
        bool parkHolds(const GateInputs &in);
        // Open: only with a safe verdict (WeatherNotSafe) and daylight not
        // wanting the roof closed (Daylight; the weather is named first).
        // Close: only while the park holds. Stop and abort are never gated.
        GateRefusal gateCommand(Roof::Command command, const GateInputs &in);
        // A close is under way (including its dead time) and the park no longer holds.
        bool interlockLost(const Roof::Status &status, const GateInputs &in);
        // Which inputs fail the park now (InterlockBit), 0 when it holds.
        uint32_t interlockBits(const GateInputs &in);
        // An open is under way and the verdict is no longer safe, or daylight
        // wants the roof closed: stop it.
        bool openMustStop(const Roof::Status &status, const GateInputs &in);
        // Why an open would be refused now (None, WeatherNotSafe or Daylight):
        // the reason an open under way was stopped.
        GateRefusal openRefusal(const GateInputs &in);

        // "none", "weatherNotSafe", "mountNotParked", "daylight".
        const char *gateRefusalName(GateRefusal refusal);
    } // namespace RoofIo
} // namespace SQM
