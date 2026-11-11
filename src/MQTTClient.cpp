#include "MQTTClient.h"
#include "NetAddress.h"
#include "Logger.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <time.h>

namespace SQM
{

    MQTTClient::MQTTClient(const MQTTConfig &config)
        : enabledNow(config.enabled),
          config(config),
          mqttClient(std::make_unique<PubSubClient>(wifiClient)),
          lastReconnectAttempt(0),
          lastPublish(0)
    {
        mqttClient->setCallback([this](char *topic, uint8_t *payload, unsigned int length) { dispatch(topic, payload, length); });
    }

    void MQTTClient::dispatch(const char *topic, const uint8_t *payload, unsigned int length)
    {
        const std::string text(reinterpret_cast<const char *>(payload), length);
        if (commandHandler && config.topic + "/" + commandSubtopic == topic)
        {
            commandHandler(text);
            return;
        }
        if (watchHandler)
            watchHandler(topic, text);
    }

    // The command topic and every watched topic, after a connect.
    void MQTTClient::subscribeAll()
    {
        if (commandHandler)
        {
            const std::string topic = config.topic + "/" + commandSubtopic;
            mqttClient->subscribe(topic.c_str(), 1);
        }
        for (const std::string &topic : watchedTopics)
            mqttClient->subscribe(topic.c_str(), 1);
    }

    void MQTTClient::watch(const std::vector<std::string> &topics, TopicHandler handler)
    {
        const bool connected = config.enabled && mqttClient->connected();
        if (connected)
            for (const std::string &topic : watchedTopics)
                mqttClient->unsubscribe(topic.c_str());
        watchedTopics = topics;
        watchHandler = std::move(handler);
        if (connected)
            for (const std::string &topic : watchedTopics)
                mqttClient->subscribe(topic.c_str(), 1);
    }

    void MQTTClient::resolveBroker()
    {
        Net::Host host;
        if (Net::parseHost(config.broker, host) == Net::HostError::None)
        {
            brokerHost = host.name;
            brokerPort = host.port != 0 ? host.port : config.port;
            return;
        }
        brokerHost = config.broker;
        brokerPort = config.port;
    }

    void MQTTClient::begin()
    {
        if (!config.enabled)
        {
            Logger::info(TAG, "MQTT disabled");
            return;
        }

        Logger::info(TAG, "Initializing MQTT client");
        configureClient();
        connect();
    }

    // The broker and the client's limits. Also when MQTT is switched on by a
    // settings save: without the 3 s socket timeout a dead broker would hold
    // the main loop for PubSubClient's default 15 s.
    void MQTTClient::configureClient()
    {
        resolveBroker();
        mqttClient->setServer(brokerHost.c_str(), brokerPort);
        mqttClient->setBufferSize(3072); // Fits the largest MQTT document (diagnostics, discovery)
        mqttClient->setKeepAlive(60);
        mqttClient->setSocketTimeout(SOCKET_TIMEOUT_S);
    }

    void MQTTClient::onCommand(const std::string &subtopic, CommandHandler handler)
    {
        commandSubtopic = subtopic;
        commandHandler = std::move(handler);
        if (config.enabled && mqttClient->connected())
        {
            const std::string topic = config.topic + "/" + commandSubtopic;
            mqttClient->subscribe(topic.c_str(), 1);
        }
    }

    void MQTTClient::handle()
    {
        applyPendingConfig();
        if (!config.enabled)
            return;

        if (!mqttClient->connected())
        {
            reconnect();
        }
        else
        {
            mqttClient->loop();
        }
    }

    bool MQTTClient::isConnected() const
    {
        return enabledNow.load() && mqttClient->connected();
    }

    MQTTStatus MQTTClient::getStatus() const
    {
        std::lock_guard<std::mutex> guard(configLock);
        MQTTStatus status;
        status.enabled = config.enabled;
        status.connected = mqttClient->connected();
        status.state = mqttClient->state();
        status.lastPublishMs = lastPublish;
        status.lastReconnectAttemptMs = lastReconnectAttempt;
        status.broker = config.broker;
        status.port = config.port;
        status.topic = config.topic;
        status.availabilityTopic = getAvailabilityTopic();
        status.clientId = buildClientId();
        return status;
    }

    void MQTTClient::updateConfig(const MQTTConfig &newConfig)
    {
        std::lock_guard<std::mutex> guard(pendingLock);
        pendingConfig = newConfig;
    }

    void MQTTClient::applyPendingConfig()
    {
        std::optional<MQTTConfig> next;
        {
            std::lock_guard<std::mutex> guard(pendingLock);
            next.swap(pendingConfig);
        }
        if (next)
            applyConfig(*next);
    }

    // Main loop: disconnect from the old broker, then connect to the new one
    // (the roof's connect hold applies; if held, the 5 s retry takes over).
    void MQTTClient::applyConfig(const MQTTConfig &newConfig)
    {
        if (mqttClient->connected())
            publishAvailability(false); // still to the old broker, under the old topic
        {
            std::lock_guard<std::mutex> guard(configLock);
            config = newConfig;
            enabledNow = config.enabled;
        }
        Logger::info(TAG, "MQTT settings applied");

        if (config.enabled)
        {
            if (mqttClient->connected())
                mqttClient->disconnect();
            configureClient();
            lastReconnectAttempt = millis();
            connect();
        }
        else
        {
            mqttClient->disconnect();
        }
    }

    void MQTTClient::connect()
    {
        if (!config.enabled)
            return;
        // Marked in flight before the hold is asked, so a close accepted in
        // between sees the connect and waits for it; cleared if held.
        connectInFlight = true;
        if (connectHold && connectHold())
        {
            connectInFlight = false;
            return;
        }

        Logger::info(TAG, "Connecting to MQTT broker: %s port %d", config.broker.c_str(), brokerPort);

        const std::string clientId = buildClientId();
        const std::string availabilityTopic = getAvailabilityTopic();

        bool connected = false;
        if (!config.username.empty())
        {
            connected = mqttClient->connect(
                clientId.c_str(), config.username.c_str(), config.password.c_str(), availabilityTopic.c_str(), 1, true, "offline");
        }
        else
        {
            connected = mqttClient->connect(clientId.c_str(), availabilityTopic.c_str(), 1, true, "offline");
        }
        connectInFlight = false;

        if (connected)
        {
            Logger::info(TAG, "Connected to MQTT broker as %s", clientId.c_str());
            publishAvailability(true);
            subscribeAll();
            ++connections;
        }
        else
        {
            Logger::error(TAG, "Failed to connect to MQTT broker, state: %d", mqttClient->state());
        }
    }

    void MQTTClient::reconnect()
    {
        const uint32_t now = millis();

        if (now - lastReconnectAttempt < RECONNECT_INTERVAL_MS)
        {
            return;
        }

        lastReconnectAttempt = now;
        Logger::info(TAG, "Attempting MQTT reconnection...");
        connect();
    }

    void MQTTClient::publishAvailability(bool online)
    {
        if (!config.enabled || !mqttClient->connected())
        {
            return;
        }

        const std::string availabilityTopic = getAvailabilityTopic();
        const char *payload = online ? "online" : "offline";
        if (!mqttClient->publish(availabilityTopic.c_str(), payload, true))
        {
            Logger::warn(TAG, "Failed to publish MQTT availability: %s", payload);
        }
    }

    std::string MQTTClient::buildClientId() const
    {
        const uint32_t macSuffix = static_cast<uint32_t>(ESP.getEfuseMac() & 0xFFFFFFULL);
        char id[24];
        snprintf(id, sizeof(id), "RoofRoof-%06lX", static_cast<unsigned long>(macSuffix));
        return std::string(id);
    }

    bool MQTTClient::publishSubtopic(const std::string &subtopic, const std::string &payload, bool retained)
    {
        if (!config.enabled || !mqttClient || !mqttClient->connected())
            return false;
        return publishTopic(config.topic + "/" + subtopic, payload, retained);
    }

    bool MQTTClient::publishTopic(const std::string &topic, const std::string &payload, bool retained)
    {
        if (!config.enabled || !mqttClient || !mqttClient->connected())
            return false;
        const bool ok = mqttClient->publish(topic.c_str(), reinterpret_cast<const uint8_t *>(payload.data()), payload.size(), retained);
        if (ok)
            lastPublish = millis();
        return ok;
    }

    std::string MQTTClient::getAvailabilityTopic() const
    {
        return config.topic + "/availability";
    }

} // namespace SQM
