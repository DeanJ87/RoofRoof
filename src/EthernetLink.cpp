#include "EthernetLink.h"

#include <Arduino.h>
#include <Network.h>
#include <atomic>
#include <esp_eth.h>
#include <esp_event.h>
#include <esp_netif.h>

#include "Logger.h"
#include "PinMap.h"
#include "PinSetup.h"

// ESP-IDF's own driver rather than Arduino's ETH class: only the LAN87xx PHY
// and the EMAC are linked (the class links every PHY it supports).

namespace SQM
{
    namespace EthernetLink
    {
        namespace
        {
            constexpr const char *TAG = "Ethernet";
            bool isPresent = false;
            bool ipv6On = false;
            esp_eth_handle_t handle = nullptr;
            esp_netif_t *netif = nullptr;
            std::atomic<bool> driverStarted{false};
            std::atomic<bool> link{false};
            std::atomic<bool> addressed{false};
            std::atomic<bool> addressedOnce{false};
            std::atomic<uint32_t> downSinceMs{0};
            std::atomic<uint32_t> linkSinceMs{0};

            void lost(const char *what)
            {
                if (addressed.exchange(false))
                    downSinceMs = millis();
                Logger::warn(TAG, "%s", what);
            }

            void onEthEvent(void *, esp_event_base_t, int32_t id, void *)
            {
                if (id == ETHERNET_EVENT_CONNECTED)
                {
                    linkSinceMs = millis();
                    link = true;
                    if (ipv6On)
                        esp_netif_create_ip6_linklocal(netif);
                    Logger::info(TAG, "Link up: %u Mbit/s", static_cast<unsigned>(speedMbps()));
                }
                else if (id == ETHERNET_EVENT_DISCONNECTED || id == ETHERNET_EVENT_STOP)
                {
                    link = false;
                    lost("Link down");
                }
            }

            void onIpEvent(void *, esp_event_base_t, int32_t id, void *data)
            {
                if (id == IP_EVENT_ETH_GOT_IP)
                {
                    addressed = true;
                    addressedOnce = true;
                    // Outbound traffic (N.I.N.A., SQMeter, alerts) prefers the cable over a fallback Wi-Fi.
                    esp_netif_set_default_netif(netif);
                    const auto *got = static_cast<const ip_event_got_ip_t *>(data);
                    Logger::info(TAG, "Got IP address: " IPSTR, IP2STR(&got->ip_info.ip));
                }
                else if (id == IP_EVENT_ETH_LOST_IP)
                {
                    lost("Address lost");
                }
            }

            bool install(const Config &config)
            {
                eth_mac_config_t macConfig = ETH_MAC_DEFAULT_CONFIG();
                eth_esp32_emac_config_t emac = ETH_ESP32_EMAC_DEFAULT_CONFIG();
                emac.smi_gpio.mdc_num = Board::ETH_MDC;
                emac.smi_gpio.mdio_num = Board::ETH_MDIO;
                emac.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN; // the 50 MHz clock comes in on GPIO0
                emac.clock_config.rmii.clock_gpio = EMAC_CLK_IN_GPIO;
                eth_phy_config_t phyConfig = ETH_PHY_DEFAULT_CONFIG();
                phyConfig.phy_addr = Board::ETH_PHY_ADDRESS;
                // GPIO16 switches the oscillator on; the driver drives it as the PHY's reset (low, then high).
                phyConfig.reset_gpio_num = Board::ETH_POWER;
                esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&emac, &macConfig);
                esp_eth_phy_t *phy = esp_eth_phy_new_lan87xx(&phyConfig);
                if (mac == nullptr || phy == nullptr)
                    return false;
                esp_eth_config_t ethConfig = ETH_DEFAULT_CONFIG(mac, phy);
                if (esp_eth_driver_install(&ethConfig, &handle) != ESP_OK)
                    return false;
                esp_netif_config_t netifConfig = ESP_NETIF_DEFAULT_ETH(); // "ETH_DEF": mDNS answers on it too
                netif = esp_netif_new(&netifConfig);
                if (netif == nullptr || esp_netif_attach(netif, esp_eth_new_netif_glue(handle)) != ESP_OK)
                    return false;
                esp_netif_set_hostname(netif, config.wifi.hostname.c_str());
                esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, onEthEvent, nullptr);
                esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, onIpEvent, nullptr);
                esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_LOST_IP, onIpEvent, nullptr);
                return esp_eth_start(handle) == ESP_OK;
            }
        } // namespace

        void begin(const Config &config)
        {
            // Spec 023: not in the Wi-Fi only mode.
            isPresent = PinSetup::ethernetBoard() && config.network.mode != NetworkConfig::Mode::Wifi;
            downSinceMs = millis();
            if (!isPresent)
                return;
            Network.begin(); // the TCP/IP stack and the default event loop
            ipv6On = config.wifi.ipv6;
            const bool ok = install(config);
            driverStarted = ok;
            if (ok)
                Logger::info(TAG, "Started (LAN8720 at address %u)", static_cast<unsigned>(Board::ETH_PHY_ADDRESS));
            else
                Logger::error(TAG, "Didn't start: the LAN8720 didn't answer");
        }

        bool present()
        {
            return isPresent;
        }

        bool started()
        {
            return driverStarted.load();
        }

        bool linkUp()
        {
            return started() && link.load();
        }

        uint16_t speedMbps()
        {
            eth_speed_t speed = ETH_SPEED_10M;
            if (!linkUp() || esp_eth_ioctl(handle, ETH_CMD_G_SPEED, &speed) != ESP_OK)
                return 0;
            return speed == ETH_SPEED_100M ? 100 : 10;
        }

        bool fullDuplex()
        {
            eth_duplex_t duplex = ETH_DUPLEX_HALF;
            return linkUp() && esp_eth_ioctl(handle, ETH_CMD_G_DUPLEX_MODE, &duplex) == ESP_OK && duplex == ETH_DUPLEX_FULL;
        }

        bool hasAddress()
        {
            return addressed.load();
        }

        bool everAddressed()
        {
            return addressedOnce.load();
        }

        uint32_t msSinceLink(uint32_t nowMs)
        {
            return linkUp() ? nowMs - linkSinceMs.load() : 0;
        }

        uint32_t msWithoutAddress(uint32_t nowMs)
        {
            return hasAddress() ? 0 : nowMs - downSinceMs.load();
        }

        std::vector<Net::Ipv6> ipv6Addresses()
        {
            std::vector<Net::Ipv6> addresses;
            if (!ipv6On || !started() || netif == nullptr || !hasAddress())
                return addresses;
            esp_ip6_addr_t found[LWIP_IPV6_NUM_ADDRESSES];
            const int count = esp_netif_get_all_ip6(netif, found);
            for (int i = 0; i < count; ++i)
            {
                Net::Ipv6 address{};
                memcpy(address.data(), found[i].addr, address.size());
                addresses.push_back(address);
            }
            return addresses;
        }

        void appendStatus(JsonDocument &doc)
        {
            JsonObject eth = doc.createNestedObject("ethernet");
            eth["present"] = isPresent;
            eth["started"] = started();
            eth["phyFound"] = started();
            eth["link"] = linkUp();
            eth["speedMbps"] = speedMbps();
            eth["fullDuplex"] = fullDuplex();
            char text[20] = "";
            esp_netif_ip_info_t ip{};
            if (hasAddress() && esp_netif_get_ip_info(netif, &ip) == ESP_OK)
                snprintf(text, sizeof(text), IPSTR, IP2STR(&ip.ip));
            eth["ip"] = std::string(text);
            uint8_t mac[6] = {};
            text[0] = '\0';
            if (started() && esp_eth_ioctl(handle, ETH_CMD_G_MAC_ADDR, mac) == ESP_OK)
                snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            eth["mac"] = std::string(text);
            JsonArray ipv6 = eth.createNestedArray("ipv6");
            for (const Net::Ipv6 &address : ipv6Addresses())
                ipv6.add(Net::formatIpv6(address));
        }
    } // namespace EthernetLink
} // namespace SQM
