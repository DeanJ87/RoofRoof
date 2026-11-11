#include "SafetyInputs.h"

#include "Logger.h"
#include "MQTTClient.h"
#include <vector>

namespace SQM
{
    namespace
    {
        constexpr const char *TAG = "Safety";

        SafetyPoller::Targets targetsFor(const SafetyConfig &safety, const ParkConfig &park)
        {
            SafetyPoller::Targets targets;
            targets.sqmeterEnabled = safety.httpEnabled;
            targets.sqmeterUrl = safety.httpUrl;
            targets.ninaUrl = park.ninaUrl;
            targets.ninaApi = Nina::ApiVersion::V2;
            return targets;
        }

        bool sameMqttSource(const SafetyConfig &a, const SafetyConfig &b)
        {
            return a.mqttEnabled == b.mqttEnabled && a.mqttTopic == b.mqttTopic;
        }

        bool sameSources(const SafetyConfig &a, const SafetyConfig &b)
        {
            return sameMqttSource(a, b) && a.httpEnabled == b.httpEnabled && a.httpUrl == b.httpUrl && a.staleSeconds == b.staleSeconds &&
                   a.rainContact == b.rainContact;
        }

        // A change worth a log line: not the ages, which tick every second.
        bool sameWeather(const Safety::WeatherVerdict &a, const Safety::WeatherVerdict &b)
        {
            return a.verdict == b.verdict && a.reasons == b.reasons && a.http.state == b.http.state && a.mqtt.state == b.mqtt.state &&
                   a.http.sqmeterFlags == b.http.sqmeterFlags && a.mqtt.sqmeterFlags == b.mqtt.sqmeterFlags;
        }
    } // namespace

    SafetyInputs::SafetyInputs(SafetyPoller &poller, MQTTClient *mqtt)
        : poller(poller),
          mqtt(mqtt),
          pendingLock(xSemaphoreCreateMutex())
    {
    }

    SafetyInputs::~SafetyInputs()
    {
        if (pendingLock != nullptr)
            vSemaphoreDelete(pendingLock);
    }

    SafetyInputs::Settings SafetyInputs::settingsOf(const Config &config)
    {
        return Settings{config.safety, config.park};
    }

    void SafetyInputs::begin(const Config &config)
    {
        apply(settingsOf(config), SafetyPoller::nowMs());
        poller.begin(targetsFor(settings.safety, settings.park));
    }

    void SafetyInputs::updateConfig(const Config &config)
    {
        if (pendingLock == nullptr)
            return;
        xSemaphoreTake(pendingLock, portMAX_DELAY);
        pending = settingsOf(config);
        xSemaphoreGive(pendingLock);
    }

    // On the main loop. Only a change to the weather sources starts the
    // weather window again (a source just switched on is unknown, never safe,
    // until it answers or its stale time passes); an unrelated save keeps
    // every reading. The poller does the same for its own parts.
    void SafetyInputs::apply(const Settings &next, uint64_t nowMs)
    {
        const bool first = !started;
        const bool sourcesChanged = first || !sameSources(settings.safety, next.safety);
        const bool mqttChanged = first || !sameMqttSource(settings.safety, next.safety);
        settings = next;
        started = true;
        if (sourcesChanged)
            startedAtMs = nowMs;
        if (mqttChanged)
        {
            mqttRecord = Safety::SourceRecord{};
            // dep: D-40 - the topics are read from RoofRoof's own broker, so with
            // MQTT off they never arrive and the source goes stale (unsafe).
            mqttRecord.enabled = settings.safety.mqttEnabled;
            topics = Safety::MqttTopics::under(settings.safety.mqttTopic);
            watchTopics();
        }
        if (!first)
            poller.configure(targetsFor(settings.safety, settings.park));
        evaluated = false;
    }

    void SafetyInputs::watchTopics()
    {
        if (mqtt == nullptr)
            return;
        std::vector<std::string> wanted;
        if (settings.safety.mqttEnabled)
            wanted = {topics.availability, topics.safe, topics.safety};
        mqtt->watch(wanted, [this](const std::string &topic, const std::string &payload) { onMqttMessage(topic, payload); });
        mqttRecord.resubscribed();
        seenConnections = mqtt->connectionCount();
    }

    void SafetyInputs::onMqttMessage(const std::string &topic, const std::string &payload)
    {
        Safety::applyMqttMessage(mqttRecord, topics, topic, payload, SafetyPoller::nowMs());
    }

    void SafetyInputs::setParkSwitches(Safety::SwitchReading first, Safety::SwitchReading second)
    {
        switch1 = first;
        switch2 = second;
    }

    void SafetyInputs::handle()
    {
        const uint64_t now = SafetyPoller::nowMs();
        std::optional<Settings> next;
        if (pendingLock != nullptr && xSemaphoreTake(pendingLock, 0) == pdTRUE)
        {
            next.swap(pending);
            xSemaphoreGive(pendingLock);
        }
        if (next)
            apply(*next, now);
        // A reconnect re-subscribes, and the broker replays its retained copies next.
        if (mqtt != nullptr && mqtt->connectionCount() != seenConnections)
        {
            seenConnections = mqtt->connectionCount();
            mqttRecord.resubscribed();
        }
        if (evaluated && now - lastEvaluateMs < EVALUATE_EVERY_MS)
            return;
        lastEvaluateMs = now;
        evaluated = true;
        evaluate(now);
        logChanges();
    }

    void SafetyInputs::evaluate(uint64_t nowMs)
    {
        const SafetyPoller::Snapshot snap = poller.snapshot();

        Safety::WeatherInputs in;
        in.http = snap.sqmeter;
        in.mqtt = mqttRecord;
        in.rainContact = settings.safety.rainContact ? rainReading : Safety::RainContact::NotFitted;
        in.httpLimitMs = settings.safety.staleSeconds * 1000;
        in.startedAtMs = startedAtMs;
        in.nowMs = nowMs;
        verdict = Safety::evaluateWeather(in);

        Safety::ParkInputs park;
        park.nina = snap.nina;
        park.fittedSwitches = static_cast<uint8_t>(settings.park.switches);
        park.switch1 = switch1;
        park.switch2 = switch2;
        park.nowMs = nowMs;
        parkResult = Safety::evaluatePark(park);
    }

    void SafetyInputs::logChanges()
    {
        if (!logged || !sameWeather(verdict, loggedVerdict))
            Logger::info(TAG, "Weather %s", Safety::describe(verdict).c_str());
        if (!logged || parkResult.parked != loggedPark.parked || parkResult.reasons != loggedPark.reasons)
            Logger::info(TAG, "Park: %s", Safety::describe(parkResult).c_str());
        logged = true;
        loggedVerdict = verdict;
        loggedPark = parkResult;
    }
} // namespace SQM
