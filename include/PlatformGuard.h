#pragma once

// The one place the firmware asks whether a platform action may run now and
// registers it before a roof command can start (spec 010). The rules are
// lib/RoofIoLogic's PlatformHold; the roof's part is checked under the roof's
// lock (RoofControl::whileStill), the close sequence's busy flag outside it.
// Any task.

#include <cstdint>
#include <functional>

#include "PlatformHold.h"

namespace SQM
{
    class RoofControl;

    namespace PlatformGuard
    {
        // setup(), before the web server starts: the roof and whether the
        // close sequence is busy. Without them nothing is held.
        // `restartCommitted`: no flash write starts once a restart is committed
        // (the reset would cut it off).
        void begin(RoofControl *roof, std::function<bool()> sequenceBusy, std::function<bool()> restartCommitted);

        // Why `action` must wait now, from the latest state (no lock): for
        // refusing a request up front. Use FlashWrite/beginUpload/commitRestart to act.
        RoofIo::HoldReason hold(RoofIo::PlatformAction action);

        // Any other action that a command must see once it has begun (a WiFi
        // change, a scan): under the roof's lock, if nothing holds `action`,
        // runs `mark` (non-blocking; it sets the caller's own flag, which the
        // command blockers read) and returns None; else why not.
        RoofIo::HoldReason claim(RoofIo::PlatformAction action, const std::function<void()> &mark);

        // A short flash write for this scope: checked and counted under the
        // roof's lock, released when it goes out of scope. Check refused() first.
        class FlashWrite
        {
        public:
            explicit FlashWrite(RoofIo::PlatformAction action = RoofIo::PlatformAction::FlashWrite);
            ~FlashWrite();
            FlashWrite(const FlashWrite &) = delete;
            FlashWrite &operator=(const FlashWrite &) = delete;
            RoofIo::HoldReason refused() const;

        private:
            RoofIo::HoldReason reason = RoofIo::HoldReason::None;
            bool held = false;
            bool refusedForRestart = false;
        };

        // An upload or command-line OTA: its flash activity token (FlashGuard),
        // begun under the roof's lock, or 0 with `why` set. With `closedOnly`
        // the roof must also read closed (command-line OTA, spec 003 FR-030).
        uint32_t beginUpload(RoofIo::PlatformAction action, RoofIo::HoldReason &why, bool closedOnly = false);

        // Each chunk of an upload: under the roof's lock, stamps its progress
        // (FlashGuard::progress) if the roof is still at rest and no close
        // waits, else returns why not (the chunk must not be written).
        RoofIo::HoldReason keepUpload(uint32_t token);

        // The restart timer: runs `commit` under the roof's lock if the roof is
        // at rest, no close waits and (unless `mandatory`) the close sequence
        // is idle and no flash write runs. Never waits for the lock: a busy
        // lock counts as Held.
        enum class RestartGo : uint8_t
        {
            Run,    // `commit` returned true: restart now
            Held,   // try again shortly
            NotDue, // `commit` returned false (cancelled meanwhile)
        };
        RestartGo commitRestart(bool mandatory, const std::function<bool()> &commit);
    } // namespace PlatformGuard
} // namespace SQM
