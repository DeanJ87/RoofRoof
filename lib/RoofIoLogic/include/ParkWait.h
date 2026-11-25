#pragma once

// Spec 027 (constitution 7.0.0, II): an Open or Close refused only because
// the park isn't confirmed waits for it when N.I.N.A. can be asked: the park
// is requested (again every gap), and once it has held for the hold time the
// command runs through every check again. The wait ends at its time limit
// (the critical "mount not parked" alert), on any stop, a new command, or
// another check refusing the command. It never drives anything itself. Pure:
// time and the park come in as parameters (constitution V).

#include <cstdint>
#include <string>

#include "RoofTypes.h"

namespace SQM
{
    namespace RoofIo
    {
        // The reason name and pending name a waiting command answers with.
        constexpr const char *PARK_WAIT_PENDING = "park";

        struct ParkWaitSettings
        {
            uint32_t limitMs = 240000; // park.timeoutSeconds
            uint32_t gapMs = 30000;    // autoClose.parkRequestSeconds
            uint32_t holdMs = 4000;    // autoClose.parkHoldSeconds
        };

        enum class ParkWaitEnd : uint8_t
        {
            None,
            Ran,       // the park held: run the command now
            GaveUp,    // the time limit passed: critical alert
            Cancelled, // a stop or a new command
            Refused,   // another check refused the command
        };

        // Where a wait stands (the state machine in spec 027's data-model.md).
        enum class ParkWaitPhase : uint8_t
        {
            Idle,    // nothing waits (also after a restart: a wait is never kept)
            Asking,  // waiting, the park not confirmed: N.I.N.A. asked, again every gap
            Holding, // the park confirmed by a reading taken since the wait began, held for the hold time
        };

        struct ParkWaitStep
        {
            bool requestPark = false; // ask N.I.N.A. to park now
            ParkWaitEnd end = ParkWaitEnd::None;
        };

        class ParkWait
        {
        public:
            // Waits for `command` (Open or Close) from `origin` with the
            // person's `overrides`; replaces a wait under way.
            void start(Roof::Command command, Roof::Origin origin, uint8_t overrides, uint32_t nowMs, const ParkWaitSettings &settings);
            // One step: `parked` is whether the park holds now.
            ParkWaitStep step(bool parked, uint32_t nowMs);
            // Ends a wait under way (a stop, a new command); `why` for the log.
            void cancel(const std::string &why);
            // The command was refused by another check (while waiting, or when
            // it ran): the wait ends with its text.
            void refuse(const std::string &text);
            bool waiting() const { return active; }
            ParkWaitPhase phase() const
            {
                return !active ? ParkWaitPhase::Idle : parkedSeen ? ParkWaitPhase::Holding : ParkWaitPhase::Asking;
            }
            Roof::Command command() const { return cmd; }
            Roof::Origin origin() const { return from; }
            uint8_t overrides() const { return carried; }
            bool asked() const { return requested; }
            // Time left before the limit (0 when not waiting).
            uint32_t leftMs(uint32_t nowMs) const;
            // The last wait ended without running (gave up or refused): an
            // Alpaca client reads an error until the next command.
            bool failed() const { return lastFailed; }
            // Why the last wait ended ("" while waiting or after it ran).
            const std::string &endText() const { return text; }
            ParkWaitEnd lastEnd() const { return ended; }

        private:
            void finish(ParkWaitEnd end, bool failure, const std::string &why);

            bool active = false;
            Roof::Command cmd = Roof::Command::Close;
            Roof::Origin from = Roof::Origin::Person;
            uint8_t carried = 0;
            ParkWaitSettings settings;
            uint32_t startedMs = 0;
            bool requested = false;
            uint32_t lastRequestMs = 0;
            bool parkedSeen = false;
            uint32_t parkedSinceMs = 0;
            bool lastFailed = false;
            ParkWaitEnd ended = ParkWaitEnd::None;
            std::string text;
        };

        // "Open" or "Close" as the wait's text names it.
        const char *commandWord(Roof::Command command);
        // The time limit's text: "The mount didn't park within 240 s: the Close wasn't run".
        std::string parkWaitGaveUpText(Roof::Command command, uint32_t limitMs);
        // Why a command refused for the park can't wait: N.I.N.A. can't be asked
        // ("" when it can). `ninaSet`: an address is set; `requestPark`: park.requestPark.
        std::string parkWaitImpossible(bool ninaSet, bool requestPark);
    } // namespace RoofIo
} // namespace SQM
