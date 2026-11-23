#pragma once

// Wi-Fi beside Ethernet on a board that has it (spec 023 FR-022 to FR-024,
// research R9): when to start the Wi-Fi station and when to open the setup
// hotspot. A board without Ethernet, or the Wi-Fi only mode, keeps
// WiFiManager's own rules.

#include <cstdint>

#include "CaptivePortal.h"

namespace SQM
{
    namespace Net
    {
        // network.mode.
        enum class NetMode : uint8_t
        {
            Auto = 0,     // Ethernet; a saved Wi-Fi network once Ethernet has been down a while
            Ethernet = 1, // Ethernet only: the radio stays off, never a hotspot
            Wifi = 2,     // Wi-Fi only: Ethernet isn't started
        };

        // Ethernet without a link or an address this long starts a saved Wi-Fi network.
        constexpr uint32_t ETHERNET_FALLBACK_MS = 30000;
        // A link gets this long to be given an address (DHCP) before the hotspot may open.
        constexpr uint32_t ADDRESS_WAIT_MS = 20000;

        struct PlanInputs
        {
            NetMode mode = NetMode::Auto;
            bool hasCredentials = false;  // a Wi-Fi network is saved
            bool ethernetLink = false;    // the cable is in and the PHY sees a link
            bool ethernetUp = false;      // Ethernet has an address
            uint32_t msEthernetDown = 0;  // since boot, or since Ethernet lost its address
            uint32_t msSinceLink = 0;     // since the link came up (with a link)
            uint32_t msSinceBoot = 0;
            bool joinedSinceBoot = false; // Ethernet or Wi-Fi had an address since boot
            bool stationStarted = false;
            bool hotspotOpen = false;
        };

        struct Plan
        {
            bool startStation = false;
            bool openHotspot = false;
        };

        Plan wifiPlan(const PlanInputs &in);
    } // namespace Net
} // namespace SQM
