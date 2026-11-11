#pragma once

#include <atomic>

#include "Config.h"
#include <PubSubClient.h>
#include <functional>
#include "DualStackClient.h"
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace SQM
{

    struct MQTTStatus
    {
        bool enabled;
        bool connected;
        int state; // PubSubClient state code
        uint32_t lastPublishMs;
        uint32_t lastReconnectAttemptMs;
        std::string broker;
        int port;
        std::string topic;
        std::string availabilityTopic;
        std::string clientId;
    };

    class MQTTClient
    {
    public:
        explicit MQTTClient(const MQTTConfig &config);
        ~MQTTClient() = default;

        // Delete copy operations
        MQTTClient(const MQTTClient &) = delete;
        MQTTClient &operator=(const MQTTClient &) = delete;

        void begin();
        void handle();
        // While the roof moves or a close waits (spec 003), no connect is
        // tried: a connect to a dead broker (DNS, TCP connect, CONNACK) would
        // hold up the main loop, and with it the safety hand-over to the roof.
        // Asked right before every connect. Set once, before begin().
        void setConnectHold(std::function<bool()> hold) { connectHold = std::move(hold); }

        bool isConnected() const;
        bool isEnabled() const { return enabledNow.load(); }

        // Get current status
        MQTTStatus getStatus() const;

        // Publish to <base topic>/<subtopic>, or to an absolute topic (Home
        // Assistant discovery). Must be called from the task that runs
        // handle() - PubSubClient isn't thread-safe. The readings themselves
        // are built by WebServer (lib/Readings).
        bool publishSubtopic(const std::string &subtopic, const std::string &payload, bool retained);
        bool publishTopic(const std::string &topic, const std::string &payload, bool retained);

        // Any task (a settings save runs on the web server's task): only hands
        // the settings over. handle() applies them on the main loop, so the
        // reconnect never runs on the web task or races the loop (spec 010
        // FR-013; spec 004 FR-011a). The newest settings win.
        void updateConfig(const MQTTConfig &newConfig);

        // Commands from the broker: `<topic>/<subtopic>` messages go to
        // `handler` (on the task that runs handle()). Subscribed on every
        // (re)connect.
        using CommandHandler = std::function<void(const std::string &payload)>;
        void onCommand(const std::string &subtopic, CommandHandler handler);
        // Messages on other devices' topics (absolute, e.g. SQMeter's
        // `sqmeter/safe`, spec 004): `handler` gets (topic, payload) on the task
        // that runs handle(). Replaces any earlier watch; subscribed now if
        // connected and again on every (re)connect.
        using TopicHandler = std::function<void(const std::string &topic, const std::string &payload)>;
        void watch(const std::vector<std::string> &topics, TopicHandler handler);
        // Bumped on every successful connect, so retained state can be
        // republished after the broker comes back.
        uint32_t connectionCount() const { return connections; }
        // Any task: a connect attempt is under way (it blocks the task that
        // runs it for up to about 3 s plus DNS). The roof refuses to open meanwhile.
        bool connecting() const { return connectInFlight.load(); }

    private:
        std::string commandSubtopic;
        CommandHandler commandHandler;
        std::vector<std::string> watchedTopics;
        TopicHandler watchHandler;
        void dispatch(const char *topic, const uint8_t *payload, unsigned int length);
        void subscribeAll();
        uint32_t connections = 0;
        static constexpr const char *TAG = "MQTT";
        static constexpr uint32_t RECONNECT_INTERVAL_MS = 5000;
        // Reads (and the CONNACK wait) give up after this many seconds: well
        // under the roof's 5 s safety hand-over limit (spec 003 research R8).
        static constexpr uint16_t SOCKET_TIMEOUT_S = 3;
        std::function<bool()> connectHold;
        std::atomic<bool> connectInFlight{false};

        // Settings handed over by updateConfig(), taken by handle().
        std::mutex pendingLock;
        std::optional<MQTTConfig> pendingConfig;
        void applyPendingConfig();
        void applyConfig(const MQTTConfig &newConfig);
        // getStatus() runs on the web task: it copies `config` under this lock,
        // and the loop changes `config` only under it.
        mutable std::mutex configLock;
        std::atomic<bool> enabledNow{false};

        MQTTConfig config;          // changed on the loop only
        DualStackClient wifiClient; // IPv4, or IPv6 literals and IPv6-only names (SQMeter spec 015)
        // The broker as PubSubClient needs it: brackets off, a "[v6]:port" port applied.
        std::string brokerHost;
        uint16_t brokerPort = 0;
        void resolveBroker();
        void configureClient();
        std::unique_ptr<PubSubClient> mqttClient;

        uint32_t lastReconnectAttempt;
        uint32_t lastPublish;

        void connect();
        void reconnect();
        void publishAvailability(bool online);
        std::string buildClientId() const;
        std::string getAvailabilityTopic() const;
    };

} // namespace SQM
