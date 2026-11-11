#pragma once

#include <WiFiClient.h>
#include <lwip/sockets.h>

namespace SQM
{
    // A WiFiClient that also connects to IPv6 literals and IPv6-only names
    // (SQMeter specs/015-ipv6-dual-stack). It resolves with getaddrinfo; lwIP returns
    // the IPv4 address when a name has both, so dual-stack servers keep using
    // IPv4 and broken upstream IPv6 never delays a connection (SQMeter spec 015 FR-008). Used
    // for plain-TCP outbound connections (MQTT, http webhooks). Since
    // Arduino-ESP32 3.x (SQMeter spec 027) the TLS clients and SNTP resolve names the
    // same way on their own.
    class DualStackClient : public WiFiClient
    {
    public:
        using WiFiClient::connect;
        int connect(const char *host, uint16_t port) override;
        int connect(const char *host, uint16_t port, int32_t timeoutMs) override;

        // Spec 008: with a budget set, a write gives up after that many
        // milliseconds in total and closes the connection, instead of
        // NetworkClient's up to ten 1 s waits for a full send buffer (a
        // half-open link). The MQTT client runs on the main loop, which hands
        // the weather and park checks to the roof and must never stall for
        // seconds. 0 (the default) keeps NetworkClient's behaviour.
        void setWriteBudgetMs(uint32_t ms) { writeBudgetMs = ms; }
        using WiFiClient::write;
        size_t write(const uint8_t *buf, size_t size) override;
        size_t write(uint8_t data) override { return write(&data, 1); }

    private:
        uint32_t writeBudgetMs = 0;
        static constexpr int32_t DEFAULT_TIMEOUT_MS = 3000;
        int connectIpv6(const ::sockaddr_in6 &address, int32_t timeoutMs);
    };
} // namespace SQM
