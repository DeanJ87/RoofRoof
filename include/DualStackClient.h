#pragma once

#include <WiFiClient.h>
#include <functional>
#include <vector>
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
        // seconds. Asked at every write; 0 means no time left: the
        // connection is dropped at once. Without one (the default) writes and
        // reads are NetworkClient's.
        void setWriteBudget(std::function<uint32_t()> budgetMs) { writeBudget = std::move(budgetMs); }
        using WiFiClient::write;
        size_t write(const uint8_t *buf, size_t size) override;
        size_t write(uint8_t data) override { return write(&data, 1); }

        // With a budget set, reads come from this class's own buffer, filled
        // without waiting. While held, available() only counts what is already
        // buffered, so a reader never starts on a packet that isn't all here.
        void holdReads(bool hold) { readsHeld = hold; }
        // A whole MQTT packet is buffered (RoofIo::Mqtt::packetComplete), or nothing is.
        bool readReady();
        int available() override;
        int read() override;
        int read(uint8_t *buf, size_t size) override;
        int peek() override;
        void stop() override;

    private:
        std::function<uint32_t()> writeBudget;
        static constexpr size_t RX_LIMIT = 4096;
        std::vector<uint8_t> rx;
        size_t rxPos = 0;
        bool readsHeld = false;
        void fillRx();
        static constexpr int32_t DEFAULT_TIMEOUT_MS = 3000;
        int connectIpv6(const ::sockaddr_in6 &address, int32_t timeoutMs);
    };
} // namespace SQM
