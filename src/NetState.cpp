#include "NetState.h"

#include <WiFi.h>

#include "EthernetLink.h"
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
        } // namespace

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
