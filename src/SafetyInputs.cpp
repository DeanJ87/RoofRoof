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

        Safety::SourceSettings sourceSettingsOf(const SafetyConfig &safety, const ParkConfig &park)
        {
            Safety::SourceSettings s;
            s.httpEnabled = safety.httpEnabled;
            s.httpUrl = safety.httpUrl;
            s.mqttEnabled = safety.mqttEnabled;
            s.mqttTopic = safety.mqttTopic;
            s.staleSeconds = safety.staleSeconds;
            s.rainContact = safety.rainContact;
            s.ninaUrl = park.ninaUrl;
            s.ninaApi = static_cast<uint8_t>(park.ninaApi);
            return s;
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
          pendingLock(xSemaphoreCreateMutex()),
          reportLock(xSemaphoreCreateMutex())
    {
    }

    SafetyInputs::~SafetyInputs()
    {
        if (pendingLock != nullptr)
            vSemaphoreDelete(pendingLock);
        if (reportLock != nullptr)
            vSemaphoreDelete(reportLock);
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
    // every reading (Safety::settingsChange decides).
    void SafetyInputs::apply(const Settings &next, uint64_t nowMs)
    {
        const Safety::SourceSettings before = sourceSettingsOf(settings.safety, settings.park);
        const Safety::SettingsChange change = Safety::settingsChange(started ? &before : nullptr, sourceSettingsOf(next.safety, next.park));
        const bool first = !started;
        settings = next;
        started = true;
        if (change.window)
            startedAtMs = nowMs;
        if (change.sqmeterMqtt)
        {
            mqttRecord = Safety::SourceRecord{};
            ++mqttGeneration;
            // dep: D-40 - the topics are read from RoofRoof's own broker, so with
            // MQTT off they never arrive and the source goes stale (unsafe).
            mqttRecord.enabled = settings.safety.mqttEnabled;
            topics = Safety::MqttTopics::under(settings.safety.mqttTopic);
            watchTopics();
        }
        if (!first)
            poller.configure(targetsFor(settings.safety, settings.park), change);
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

        // Spec 015: a copy for GET /api/roof/inputs (the web server's task reads it).
        Safety::InputsReport next{verdict, parkResult, snap.nina, nowMs, snap.sqmeterGeneration, mqttGeneration, snap.ninaGeneration};
        if (reportLock != nullptr && xSemaphoreTake(reportLock, portMAX_DELAY) == pdTRUE)
        {
            lastReport = std::move(next);
            xSemaphoreGive(reportLock);
        }
    }

    Safety::InputsReport SafetyInputs::report() const
    {
        Safety::InputsReport copy;
        if (reportLock != nullptr && xSemaphoreTake(reportLock, portMAX_DELAY) == pdTRUE)
        {
            copy = lastReport;
            xSemaphoreGive(reportLock);
        }
        return copy;
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
