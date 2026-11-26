#pragma once

// Whether the device is on a network (spec 023 FR-025): the Wi-Fi station
// joined or Ethernet with an address. Everything that used to ask the Wi-Fi
// station alone asks here.

#include <vector>

#include "NetAddress.h"
#include "NetworkReturn.h"

namespace SQM
{
    namespace NetState
    {
        bool wifiUp();
        bool ethernetUp();
        bool online();
        // Spec 038/048: called once per main-loop pass; records each return
        // of the network (ConnectionBudget::NetworkReturn) from the link and
        // from the inbound connections (InboundGate).
        void tick();
        // Main loop: since the network last came back (0 while down), and
        // since the link itself did (not counting requests back on a kept link).
        uint32_t sinceReturn();
        uint32_t sinceLinkUp();
        // Any task: returns since start (ConnectionBudget::NetworkReturn::returns()).
        uint32_t returns();
        // Any task: where the device is after the last return (a pass old at most).
        ConnectionBudget::ReturnPhase phase();
        // Main loop: what the newest return's record collects.
        void noteHeap(uint32_t largestBlock, uint32_t freeBytes);
        void noteMqttConnected();
        void noteDiscovery(ConnectionBudget::DiscoveryOutcome outcome);
        // Any task: a copy of the returns' records (GET /api/diag/heap).
        void copyReturns(ConnectionBudget::NetworkReturn &out);
        bool hotspotOn();
        // Spec 023: Ethernet only is in effect since boot (the radio stays off).
        void setEthernetOnly(bool on);
        bool ethernetOnly();
        // The device's own IPv6 addresses on the station and on Ethernet.
        std::vector<Net::Ipv6> ipv6Addresses();
    } // namespace NetState
} // namespace SQM
