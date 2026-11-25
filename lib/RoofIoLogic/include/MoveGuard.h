#pragma once

// Spec 027 (constitution 7.0.0): what stands between a person's or a
// client's Open or Close and the motion logic beyond the checks themselves:
// the wait for the park (ParkWait) and a person's overrides in effect. One
// instance per roof, used under the roof's lock by the firmware (RoofControl)
// and the demo alike, so both decide the same way. Pure (constitution V).

#include <cstdint>
#include <string>

#include "CommandCheck.h"
#include "Config.h"
#include "ParkWait.h"
#include "RoofGates.h"
#include "RoofTypes.h"

namespace SQM
{
    namespace RoofIo
    {
        struct MoveGuardSettings
        {
            ParkWaitSettings wait;
            bool ninaSet = false;    // a N.I.N.A. address is set
            bool requestPark = true; // park.requestPark
        };

        // park.timeoutSeconds, autoClose.parkRequestSeconds and .parkHoldSeconds,
        // the N.I.N.A. address and park.requestPark.
        MoveGuardSettings moveGuardSettingsOf(const Config &config);

        enum class Admit : uint8_t
        {
            Run,    // run the command now (the check passed)
            Wait,   // accepted: waiting for the park
            Refuse, // refused: `text`
        };

        struct Admission
        {
            Admit action = Admit::Refuse;
            std::string reason; // the refusal's machine name
            std::string text;
        };

        // What a step asks of the roof.
        struct GuardStep
        {
            bool requestPark = false; // ask N.I.N.A. to park (the main loop sends it)
            bool run = false;         // the wait is over: run command() from origin() with overrides()
            bool gaveUp = false;      // the critical "mount not parked" alert, with text()
            bool refused = false;     // the waiting command was refused by another check
        };

        class MoveGuard
        {
        public:
            void configure(const MoveGuardSettings &next) { settings = next; }
            // An Open or Close from anyone but the automatic close: `check` with
            // the person's overrides, `parkAside` the same with the park set
            // aside. A different command replaces a wait under way; the same
            // command refused for the park again joins it (no second park
            // request, the time limit unchanged).
            struct Request
            {
                Roof::Command command = Roof::Command::Close;
                Roof::Origin origin = Roof::Origin::Person;
                uint8_t overrides = 0;
            };
            Admission admit(const Request &request, const CommandCheck &check, const CommandCheck &parkAside, uint32_t nowMs);
            // Spec 027: the automatic close is about to run: a waiting Open is
            // called off (a close always wins); a waiting Close carries on.
            void closeWins();
            // The command ran and was accepted: its overrides take effect (an
            // Open's weather and daylight, a Close's park), as `gates` show now.
            void ran(Roof::Command command, uint8_t overrides, const GateInputs &gates);
            // Any accepted stop or abort.
            void stopped();
            // One control step. `waitCheck`: the waiting command's check with
            // the park set aside (ignored while nothing waits).
            GuardStep step(
                const Roof::Status &status, bool closePending, const GateInputs &gates, const CommandCheck &waitCheck, uint32_t nowMs);
            // The waiting command ran and was refused: the wait failed with `text`.
            void runRefused(const std::string &text) { parkWait.refuse(text); }

            const ParkWait &wait() const { return parkWait; }
            // An Open override in effect (OverrideWeather, OverrideDaylight).
            uint8_t openOverride() const { return openBits; }
            // A person's confirmed close is under way: no park re-check.
            bool closeParkOverride() const { return closeAside; }

        private:
            MoveGuardSettings settings;
            ParkWait parkWait;
            uint8_t openBits = 0;
            bool closeAside = false;
        };

        // Spec 027: the body of POST /api/roof/open or /close: none, or
        // {"override": ["weatherNotSafe", "daylight"]} (or ["mountNotParked"]).
        // False, with `error`, for a body that isn't that shape or names
        // something that can't be overridden.
        bool parseOverrides(const std::string &body, uint8_t &bits, std::string &error);

        // "Waiting for the mount to park: N.I.N.A. was asked; the Close runs once the park holds".
        std::string parkWaitText(Roof::Command command);
        // The override names in effect, for the status ("weatherNotSafe", "daylight", "mountNotParked").
        const char *overrideName(uint8_t bit);
    } // namespace RoofIo
} // namespace SQM
