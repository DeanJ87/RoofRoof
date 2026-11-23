#pragma once

// Whether the device is on a network (spec 023 FR-025): the Wi-Fi station
// joined or Ethernet with an address. Everything that used to ask the Wi-Fi
// station alone asks here.

#include <vector>

#include "NetAddress.h"

namespace SQM
{
    namespace NetState
    {
        bool wifiUp();
        bool ethernetUp();
        bool online();
        bool hotspotOn();
        // Spec 023: Ethernet only is in effect since boot (the radio stays off).
        void setEthernetOnly(bool on);
        bool ethernetOnly();
        // The device's own IPv6 addresses on the station and on Ethernet.
        std::vector<Net::Ipv6> ipv6Addresses();
    } // namespace NetState
} // namespace SQM
