#pragma once

// Which platform actions wait for the roof and for the automatic close (spec
// 010, contracts/platform-hold.md). Flash writes stall the control task and its
// watchdog; a restart cuts the driver mid-move and costs the close sequence its
// wait; a new firmware may not read spec 005's RTC record; a Wi-Fi change drops
// the network the park check needs (constitution I, III). The firmware checks
// the roof part again under the roof's lock (RoofControl::whileStill).

#include <cstdint>

#include "RoofTypes.h"

namespace SQM
{
    namespace RoofIo
    {
        enum class PlatformAction : uint8_t
        {
            FlashWrite,       // a settings save, restore or NVS write
            FilesystemUpload, // a web UI upload: always ends in a mandatory restart
            Restart,          // any restart but the one after a web UI upload
            MandatoryRestart, // after a web UI upload: remounts the filesystem
            FirmwareUpload,   // web or command-line
            WifiChange,       // trying another network
            WifiScan,         // a scan (the radio leaves the channel for a while)
        };

        // Why an action must wait; the first that applies, in this order.
        enum class HoldReason : uint8_t
        {
            None,
            Moving,        // the roof is opening or closing
            CloseWaiting,  // a close waits for an upload, a write or an MQTT connect
            CloseSequence, // spec 005's sequence is in any phase but idle
        };

        struct PlatformState
        {
            Roof::State roof = Roof::State::Unknown;
            bool closeWaiting = false;
            bool sequenceBusy = false;
        };

        HoldReason platformHold(PlatformAction action, const PlatformState &state);
        // The 409 text, "" for None.
        const char *holdText(HoldReason reason);
        const char *holdName(HoldReason reason);
        const char *platformActionName(PlatformAction action);

        // What the restart timer checks under the roof's lock, beyond the
        // roof itself (spec 010).
        struct RestartInputs
        {
            bool mandatory = false;    // after a web UI upload: never cancelled
            bool sequenceBusy = false; // spec 005's sequence is not idle
            bool uploadBusy = false;   // an upload or command-line OTA writes the flash
            bool shortWrite = false;   // a settings save, an NVS value, the fault record
            bool faultUnsaved = false; // a latched fault isn't in NVS yet
        };
        // True when the restart may commit now. A latched fault not yet saved
        // holds every restart (a restart never clears the lockout); so does a
        // short write (half-written NVS). An ordinary restart also waits for
        // the close sequence and an upload; the mandatory one follows an upload
        // that has already ended.
        bool restartMayCommit(const RestartInputs &in);

        // A WiFi change or scan holds a close at most this long from its start,
        // well inside the close sequence's 30 s limit for a waiting close to start.
        constexpr uint32_t NETWORK_HOLD_MAX_MS = 15000;
        // Whether a WiFi change or scan started `ageMs` ago still holds a close.
        bool networkHoldsClose(bool changing, uint32_t ageMs);

        // The setup hotspot has no password: while it is on, open is refused
        // (from any network: a client on the hotspot can reach the device's
        // other addresses too). Stop, close and clear-fault help the roof end
        // closed and stay.
        constexpr const char *HOTSPOT_REASON = "hotspot";
        constexpr const char *HOTSPOT_TEXT = "Opening isn't available while the setup hotspot is on";
        bool hotspotRefuses(Roof::Command command, bool hotspotOn);
    } // namespace RoofIo
} // namespace SQM
