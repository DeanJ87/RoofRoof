#pragma once

// The WT32-ETH01's wired Ethernet (spec 023 FR-021, FR-025, research R8): the
// LAN8720 on the ESP32's EMAC, started only when the board in effect has it.

#include <ArduinoJson.h>
#include <cstdint>
#include <vector>

#include "Config.h"
#include "NetAddress.h"

namespace SQM
{
    namespace EthernetLink
    {
        // setup(), after the pins: on the WT32-ETH01 board (chosen or, with
        // none chosen, detected), unless network.mode is Wi-Fi only.
        void begin(const Config &config);

        // Main loop: the default route on Ethernet while it has an address, else on the Wi-Fi station.
        void keepDefaultRoute(bool wifiUp);

        bool present(); // the board in effect has Ethernet
        bool started(); // the driver started (the PHY answered)
        bool linkUp();
        uint16_t speedMbps(); // 0 without a link
        bool fullDuplex();
        bool hasAddress(); // an IPv4 address: Ethernet counts as a network
        bool everAddressed();
        // How long Ethernet has been without an address (since boot, or since
        // it lost it); 0 while it has one.
        uint32_t msWithoutAddress(uint32_t nowMs);
        // How long the link has been up; 0 without one.
        uint32_t msSinceLink(uint32_t nowMs);
        std::vector<Net::Ipv6> ipv6Addresses();

        // The status document's `ethernet` block.
        void appendStatus(JsonDocument &doc);
    } // namespace EthernetLink
} // namespace SQM
