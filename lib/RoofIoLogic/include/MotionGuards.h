#pragma once

// What the rest of the firmware must not do while the roof moves, and the
// small checks around the motor driver (spec 003 FR-024 to FR-028, added
// after the PR review): flash writes stall both cores' caches (and with them
// the control task and its watchdog), so settings saves, restores, uploads,
// restarts and other NVS writes wait until the roof is at rest; roof commands
// need a header a cross-site form can't send; the watchdog must itself be
// alive before the driver may switch on; and the motor current read under
// PWM is scaled back to the running current.

#include <cstdint>

#include "RoofTypes.h"

namespace SQM
{
    namespace RoofIo
    {
        // The roof is opening or closing (including the dead time before the motor starts).
        bool roofMoving(Roof::State state);

        // Settings saves, restores, firmware and web UI uploads and restarts
        // are refused while the roof moves (and other NVS writes wait).
        bool platformActionRefused(Roof::State state);

        // An open or close is refused while a flash write is under way (an
        // upload, a backup job); stop never is.
        bool flashBusyRefuses(Roof::Command command, bool flashBusy);
        constexpr const char *FLASH_BUSY_TEXT = "An upload or backup is being written: try again when it has finished";

        // Every roof command carries this header with the value "1": a
        // cross-site form or a simple cross-origin request can't set it
        // (constitution VIII).
        constexpr const char *COMMAND_HEADER = "X-RoofRoof";
        bool commandHeaderValid(const char *value);

        // The watchdog timer stamps a heartbeat every WATCHDOG_CHECK_MS. EN may
        // only go high while it is at most WATCHDOG_BEAT_LIMIT_MS old; the
        // control task latches a fault if it stops for WATCHDOG_LIMIT_MS while
        // the driver is on.
        constexpr uint32_t WATCHDOG_BEAT_LIMIT_MS = 30;
        bool watchdogAlive(uint32_t nowMs, uint32_t beatMs, uint32_t limitMs = WATCHDOG_BEAT_LIMIT_MS);

        // The BTS7960's current output only follows the motor while its side
        // of the bridge is on, so a reading averaged under PWM is about duty
        // × the motor current. Scaled back by the duty that was applied while
        // it was read. Driving at CURRENT_CHECK_MIN_DUTY or more for
        // CURRENT_SETTLE_MS, a reading below MIN_RUNNING_AMPS is a broken
        // sense wire, not a motor: NaN (spec 002 latches currentUnreadable).
        constexpr uint8_t CURRENT_CHECK_MIN_DUTY = 64; // 25%
        constexpr uint32_t CURRENT_SETTLE_MS = 1000;
        constexpr float MIN_RUNNING_AMPS = 0.2f;
        float runningAmps(float readAmps, const Roof::Status &status, const Roof::Output &applied);
    } // namespace RoofIo
} // namespace SQM
