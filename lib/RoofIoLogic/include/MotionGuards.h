#pragma once

// What the rest of the firmware must not do while the roof moves, and the
// checks around the motor driver (spec 003 FR-024 to FR-031, added after the
// PR reviews):
// - flash writes stall both cores' caches (and with them the control task
//   and its watchdog), so they wait until the roof is at rest, and a close
//   always wins over an upload;
// - an MQTT connect or a scheduled restart would hold up a move: open waits
//   for them, a close cancels the restart and waits for the connect;
// - roof commands need a header a cross-site form can't send;
// - the watchdog must itself be alive while the driver is on;
// - the motor current read under PWM is scaled back to the running current.

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

        // Command-line OTA (ArduinoOTA) blocks the main loop for the whole
        // upload and can't be stopped part-way, so it is only serviced while
        // the roof is closed: a close is never waiting behind it.
        bool commandLineOtaAllowed(Roof::State state);

        // A flash write in progress: a web upload or command-line OTA. It
        // counts as busy only while it makes progress: a dropped upload whose
        // connection never reported closing stops counting after FLASH_STALE_MS.
        constexpr uint32_t FLASH_STALE_MS = 5000;
        class FlashActivity
        {
        public:
            // A new activity (replacing any other); returns its token (never 0).
            uint32_t begin(uint32_t nowMs);
            // A chunk was written. Ignored for a token that isn't current.
            void progress(uint32_t token, uint32_t nowMs);
            // Finished, failed or disconnected.
            void end(uint32_t token);
            bool busy(uint32_t nowMs) const;
            // A close wants the flash: the activity stops at its next chunk.
            void requestStop();
            // This activity must stop writing (a close asked, or it was replaced or ended).
            bool shouldStop(uint32_t token) const;

            // Short writes (a settings save, a restore, an NVS value; spec 010):
            // counted while they run, never stopped, never stale (they always
            // end), and independent of the upload's token.
            void holdShort();
            void releaseShort();
            bool writing() const { return shortWrites > 0; }

        private:
            uint32_t shortWrites = 0;
            uint32_t current = 0;
            uint32_t nextToken = 1;
            uint32_t lastProgressMs = 0;
            bool active = false;
            bool stopRequested = false;
        };

        // A scheduled restart and whether a close may cancel it. Once a
        // mandatory one is due (it remounts a rewritten filesystem), a later
        // ordinary schedule never downgrades it, and cancel() refuses it.
        class RestartPlan
        {
        public:
            void schedule(bool mandatory);
            // True if it was cancelled (the caller stops its timer).
            bool cancel();
            // The timer couldn't be started, or the restart was stopped some other way.
            void clear();
            // The restart's last check passed (spec 010): it can no longer be
            // cancelled, and no move may start before the reset. False if none is due.
            bool commit();
            bool scheduled() const { return due; }
            bool cancellable() const { return due && !mustRun && !isCommitted; }
            bool mandatory() const { return due && mustRun; }
            bool committed() const { return isCommitted; }

        private:
            bool due = false;
            bool mustRun = false;
            bool isCommitted = false;
        };

        // What else is going on that a move must not overlap.
        struct Blockers
        {
            bool flashBusy = false;        // an upload or command-line OTA is writing the flash
            bool mqttConnecting = false;   // the main loop is inside an MQTT connect (bounded, about 3 s plus DNS)
            bool restartScheduled = false; // a restart is due
            // ...and a close may cancel it. A restart after a filesystem upload
            // stopped part-way can't be cancelled (the filesystem is half
            // erased): it is held while the roof moves and runs once it stops.
            bool restartCancellable = true;
            bool flashWriting = false;     // a short flash write (a settings save) is running (spec 010)
            bool restartCommitted = false; // a restart passed its last check: the reset is moments away
            bool networkChanging = false;  // trying another WiFi network or scanning: N.I.N.A. may drop (spec 010)
        };

        enum class BlockerAction : uint8_t
        {
            Proceed,
            Refuse,     // open: try again later
            DeferClose, // close: accepted, starts once nothing blocks it
        };

        struct BlockerDecision
        {
            BlockerAction action = BlockerAction::Proceed;
            bool cancelRestart = false; // a close cancels a scheduled restart
            bool stopFlash = false;     // a close stops an upload at its next chunk
        };

        // Stop and abort always proceed. Open is refused while anything blocks.
        // Close always wins: it cancels a scheduled restart, stops an upload,
        // and waits (deferred) only while a flash write or an MQTT connect is
        // still under way. A committed restart refuses close too (spec 010):
        // the reset would cut it off.
        BlockerDecision blockerDecision(Roof::Command command, const Blockers &blockers);
        // The refusal's reason name: "restarting" for a committed restart (the
        // automatic close then waits rather than counting a failed close),
        // else "busy".
        constexpr const char *RESTARTING_REASON = "restarting";
        const char *blockerReason(const Blockers &blockers);
        // "The device is restarting", "An upload is being written", "A settings
        // save is being written", "MQTT is connecting", "A restart is scheduled".
        const char *blockerText(const Blockers &blockers);

        // Every roof command carries this header with the value "1": a
        // cross-site form or a simple cross-origin request can't set it
        // (constitution VIII).
        constexpr const char *COMMAND_HEADER = "X-RoofRoof";
        bool commandHeaderValid(const char *value);

        // The watchdog timer stamps a heartbeat every WATCHDOG_CHECK_MS. EN may
        // only go high while it is at most WATCHDOG_BEAT_LIMIT_MS old: four
        // missed checks in a row, so one or two late esp_timer callbacks under
        // WiFi load never fault a move, while a stopped watchdog is still
        // caught well inside the 100 ms step-gap limit (research R15).
        constexpr uint32_t WATCHDOG_BEAT_LIMIT_MS = 50;
        bool watchdogAlive(uint32_t nowMs, uint32_t beatMs, uint32_t limitMs = WATCHDOG_BEAT_LIMIT_MS);
        // The logic wants the driver on but the watchdog is not alive: latch a
        // watchdogStopped fault rather than hold EN low silently.
        bool watchdogStoppedFault(bool outputOn, uint32_t nowMs, uint32_t beatMs);

        // The BTS7960's current output only follows the motor while its side
        // of the bridge is on, so a reading averaged under PWM is about duty
        // × the motor current: scaled back by the duty applied while it was
        // read. With `minRunningAmps` above 0 (a setting, measured on the
        // bench; 0 = off), a reading below it after CURRENT_SETTLE_MS of
        // driving is a broken sense wire: NaN (spec 002 latches
        // currentUnreadable). The ADC can't resolve the lowest currents
        // (research R13), so this is only as good as the bench measurement.
        constexpr uint32_t CURRENT_SETTLE_MS = 1000;
        float runningAmps(float readAmps, const Roof::Status &status, const Roof::Output &applied, float minRunningAmps);
    } // namespace RoofIo
} // namespace SQM
