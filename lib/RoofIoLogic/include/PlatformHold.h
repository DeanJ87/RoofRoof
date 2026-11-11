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

        // The setup hotspot has no password: while it is on, open is refused
        // (from any network: a client on the hotspot can reach the device's
        // other addresses too). Stop, close and clear-fault help the roof end
        // closed and stay.
        constexpr const char *HOTSPOT_REASON = "hotspot";
        constexpr const char *HOTSPOT_TEXT = "Opening isn't available while the setup hotspot is on";
        bool hotspotRefuses(Roof::Command command, bool hotspotOn);
    } // namespace RoofIo
} // namespace SQM
