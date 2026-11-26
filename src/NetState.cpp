#include "NetState.h"

#include <WiFi.h>

#include <atomic>

#include "EthernetLink.h"
#include "InboundGate.h"
#include "WiFiManager.h"

namespace SQM
{
    namespace NetState
    {
        bool wifiUp()
        {
            return WiFi.status() == WL_CONNECTED;
        }

        bool ethernetUp()
        {
            return EthernetLink::hasAddress();
        }

        bool online()
        {
            return wifiUp() || ethernetUp();
        }

        namespace
        {
            bool ethernetOnlyMode = false; // set once in setup()
            // Spec 048: written by the main loop under the lock, copied under it.
            ConnectionBudget::NetworkReturn netReturns;
            portMUX_TYPE returnsLock = portMUX_INITIALIZER_UNLOCKED;
            std::atomic<uint8_t> phaseNow{static_cast<uint8_t>(ConnectionBudget::ReturnPhase::Settled)};
            std::atomic<uint32_t> returnsNow{0};
        } // namespace

        void tick()
        {
            const uint32_t now = millis();
            const bool up = online();
            const uint32_t lastInbound = InboundGate::lastArrivalMs();
            const size_t live = InboundGate::live();
            const uint32_t refused = InboundGate::refused();
            portENTER_CRITICAL(&returnsLock);
            netReturns.update(up, now, lastInbound);
            netReturns.noteInbound(now, live, refused);
            const ConnectionBudget::ReturnPhase phase = netReturns.phase(now);
            const uint32_t count = netReturns.returns();
            portEXIT_CRITICAL(&returnsLock);
            phaseNow.store(static_cast<uint8_t>(phase));
            returnsNow.store(count);
        }

        uint32_t sinceReturn()
        {
            return netReturns.sinceReturn(millis()); // the main loop is the only writer
        }

        uint32_t returns()
        {
            return returnsNow.load();
        }

        uint32_t sinceLinkUp()
        {
            return netReturns.sinceLinkUp(millis());
        }

        ConnectionBudget::ReturnPhase phase()
        {
            return static_cast<ConnectionBudget::ReturnPhase>(phaseNow.load());
        }

        void noteHeap(uint32_t largestBlock, uint32_t freeBytes)
        {
            const uint32_t now = millis();
            portENTER_CRITICAL(&returnsLock);
            netReturns.noteHeap(now, largestBlock, freeBytes);
            portEXIT_CRITICAL(&returnsLock);
        }

        void noteMqttConnected()
        {
            const uint32_t now = millis();
            portENTER_CRITICAL(&returnsLock);
            netReturns.noteMqttConnected(now);
            portEXIT_CRITICAL(&returnsLock);
        }

        void noteDiscovery(ConnectionBudget::DiscoveryOutcome outcome)
        {
            portENTER_CRITICAL(&returnsLock);
            netReturns.noteDiscovery(outcome);
            portEXIT_CRITICAL(&returnsLock);
        }

        void copyReturns(ConnectionBudget::NetworkReturn &out)
        {
            portENTER_CRITICAL(&returnsLock);
            out = netReturns;
            portEXIT_CRITICAL(&returnsLock);
        }

        void setEthernetOnly(bool on)
        {
            ethernetOnlyMode = on;
        }

        bool ethernetOnly()
        {
            return ethernetOnlyMode;
        }

        bool hotspotOn()
        {
            return (WiFi.getMode() & WIFI_AP) != 0;
        }

        std::vector<Net::Ipv6> ipv6Addresses()
        {
            std::vector<Net::Ipv6> addresses = WiFiManager::ipv6Addresses();
            for (const Net::Ipv6 &address : EthernetLink::ipv6Addresses())
                addresses.push_back(address);
            return addresses;
        }
    } // namespace NetState
} // namespace SQM
