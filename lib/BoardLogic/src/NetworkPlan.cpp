#include "NetworkPlan.h"

namespace SQM
{
    namespace Net
    {
        Plan wifiPlan(const PlanInputs &in)
        {
            Plan plan;
            if (in.mode != NetMode::Auto)
                return plan; // Ethernet only: never; Wi-Fi only: WiFiManager's own rules
            plan.startStation = !in.stationStarted && in.hasCredentials && !in.ethernetUp && in.msEthernetDown >= ETHERNET_FALLBACK_MS;
            // Only at boot, only with no network at all: a link still waiting
            // for its address gets ADDRESS_WAIT_MS. After a join, outages are
            // retried, never answered with a passwordless hotspot (research R9).
            const bool addressDue = !in.ethernetLink || in.msSinceLink >= ADDRESS_WAIT_MS;
            plan.openHotspot = !in.hotspotOpen && !in.joinedSinceBoot && in.msSinceBoot >= CaptivePortal::FALLBACK_AFTER_MS && addressDue;
            return plan;
        }
    } // namespace Net
} // namespace SQM
