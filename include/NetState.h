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
        // The device's own IPv6 addresses on the station and on Ethernet.
        std::vector<Net::Ipv6> ipv6Addresses();
    } // namespace NetState
} // namespace SQM
