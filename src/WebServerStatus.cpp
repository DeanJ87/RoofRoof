#include "BenchMode.h"
#include "ConnectionBudget.h"
#include "FirmwareMarker.h"
#include "HeapTrace.h"
#include "Ipv6Network.h"
#include "Logger.h"
#include "RoofControl.h"
#include "RoofMqtt.h"
#include "WebServer.h"
#include "version.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <cstring>
#include <ctime>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <nvs.h>

extern uint32_t bootCount;

// Documents the device publishes: /api/status, the settings facts and Home
// Assistant discovery.

namespace SQM
{
    namespace
    {
        // Which entities are announced (spec 008 adds the roof's).
        HaDiscovery::Groups discoveryGroups(const Config &cfg)
        {
            HaDiscovery::Groups groups;
            groups.alertsSwitch = cfg.alerts.enabled; // dep: D-13
            groups.roofCommands = cfg.mqtt.roofCommands;
            groups.currentSense = cfg.roof.currentSenseEnabled;
            return groups;
        }

        // Everything discovery depends on: rebuilt from scratch when it changes.
        std::string discoveryKeyOf(bool on, const HaDiscovery::Device &device, const HaDiscovery::Groups &groups)
        {
            std::string key = std::to_string(on) + device.name + device.baseTopic + device.prefix;
            for (const bool group : {groups.alertsSwitch, groups.roofCommands, groups.currentSense})
                key += group ? '1' : '0';
            return key;
        }
    } // namespace

    // The roof moves, a close waits or the automatic close is attempting: a
    // burst of retained configs must not hold the main loop then (spec 008
    // research R4).
    bool WebServer::roofBusyForMqtt() const
    {
        const bool attempting = closePhase && RoofIo::Mqtt::closeAttempting(closePhase());
        return roof != nullptr && (roof->moving() || roof->closeWaiting() || attempting);
    }

    // Home Assistant discovery whenever the connection or the settings change.
    void WebServer::publishDiscovery()
    {
        if (mqttClient == nullptr || !mqttClient->isConnected() || roofBusyForMqtt())
            return;
        const Config &cfg = getConfigCallback();
        const MQTTConfig &mqtt = cfg.mqtt;
        const HaDiscovery::Groups groups = discoveryGroups(cfg);

        // Rebuilt from scratch when anything it depends on changes.
        char mac[13];
        snprintf(mac, sizeof(mac), "%012llx", static_cast<unsigned long long>(ESP.getEfuseMac()));
        HaDiscovery::Device device{
            std::string("roofroof_") + mac,
            cfg.deviceName,
            std::string(FIRMWARE_VERSION) + (Bench::BUILD ? " (bench)" : ""),
            mqtt.topic,
            mqtt.discoveryPrefix};
        const std::string key = discoveryKeyOf(mqtt.homeAssistant, device, groups);
        if (key == discoveryKey && mqttClient->connectionCount() == discoveryConnection)
            return;

        auto publish = [this](const std::string &topic, const std::string &payload) { mqttClient->publishTopic(topic, payload, true); };
        if (!discoveryKey.empty() && discoveryWasOn &&
            (!mqtt.homeAssistant || discoveryDevice.prefix != device.prefix || discoveryDevice.baseTopic != device.baseTopic))
        {
            // Remove what was announced under the old settings.
            HaDiscovery::forEach(discoveryDevice, groups, [&](const std::string &topic, const std::string &) { publish(topic, ""); });
        }
        if (mqtt.homeAssistant)
            HaDiscovery::forEach(device, groups, publish);

        discoveryKey = key;
        discoveryConnection = mqttClient->connectionCount();
        discoveryDevice = device;
        discoveryWasOn = mqtt.homeAssistant;
    }

    Deps::Facts WebServer::settingsFacts() const
    {
        Deps::Facts facts;
        facts.wifiConnected = WiFi.status() == WL_CONNECTED;
        facts.mqttConnected = mqttClient != nullptr && mqttClient->isConnected();
        facts.clockSet = static_cast<int64_t>(time(nullptr)) >= Core::CLOCK_VALID_EPOCH;
        facts.alpacaDevices = static_cast<uint8_t>(alpacaBackend.devices().size());
        return facts;
    }

    ChannelBlocks WebServer::channelBlocks() const
    {
        const std::vector<Deps::Entry> entries = Deps::evaluate(getConfigCallback(), settingsFacts());
        auto blocked = [&entries](const char *setting) -> const char *
        {
            const Deps::Reason *reason = Deps::reasonFor(entries, setting);
            return reason != nullptr && std::strcmp(reason->code, "alerts-off") != 0 ? reason->text : nullptr;
        };
        ChannelBlocks blocks{};
        blocks[static_cast<size_t>(AlertChannel::Mqtt)] = blocked("alerts.mqtt.enabled");         // dep: D-01 D-02
        blocks[static_cast<size_t>(AlertChannel::Pushover)] = blocked("alerts.pushover.enabled"); // dep: D-03
        blocks[static_cast<size_t>(AlertChannel::Ntfy)] = blocked("alerts.ntfy.enabled");
        blocks[static_cast<size_t>(AlertChannel::Webhook)] = blocked("alerts.webhook.enabled");
        return blocks;
    }

    namespace
    {
        void appendMemory(JsonDocument &doc)
        {
            JsonArray heapStages = doc.createNestedArray("heapStages");
            for (size_t i = 0; i < HeapTrace::count(); ++i)
            {
                const HeapTrace::Checkpoint &checkpoint = HeapTrace::at(i);
                JsonObject stage = heapStages.createNestedObject();
                stage["stage"] = checkpoint.stage;
                stage["free"] = checkpoint.freeBytes;
                stage["largest"] = checkpoint.largestBlock;
            }
            doc["minFreeHeap"] = ESP.getMinFreeHeap();
            doc["maxAllocHeap"] = ESP.getMaxAllocHeap();
            doc["heapSize"] = ESP.getHeapSize();
            doc["cpuFreqMHz"] = ESP.getCpuFreqMHz();
            doc["flashSize"] = ESP.getFlashChipSize();
            doc["sketchSize"] = ESP.getSketchSize();
            doc["freeSketchSpace"] = ESP.getFreeSketchSpace();
            doc["resetReason"] = static_cast<int>(esp_reset_reason());
            doc["bootCount"] = ::bootCount;

            // Filesystem stats
            doc["fsTotal"] = LittleFS.totalBytes();
            doc["fsUsed"] = LittleFS.usedBytes();
        }

        void appendPartitions(JsonDocument &doc)
        {
            // Partition information
            JsonObject partitions = doc.createNestedObject("partitions");

            // Get running OTA partition
            const esp_partition_t *running = esp_ota_get_running_partition();
            const esp_partition_t *boot = esp_ota_get_boot_partition();

            if (running)
            {
                partitions["runningSlot"] = running->label;
                partitions["runningAddress"] = running->address;
                partitions["runningSize"] = running->size;
            }

            if (boot)
            {
                partitions["bootSlot"] = boot->label;
            }

            // Get next OTA partition info
            const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
            if (next)
            {
                partitions["nextSlot"] = next->label;
                partitions["nextSize"] = next->size;
            }

            // Get NVS partition stats
            nvs_stats_t nvs_stats;
            if (nvs_get_stats(NULL, &nvs_stats) == ESP_OK)
            {
                JsonObject nvs = partitions.createNestedObject("nvs");
                nvs["usedEntries"] = nvs_stats.used_entries;
                nvs["freeEntries"] = nvs_stats.free_entries;
                nvs["totalEntries"] = nvs_stats.total_entries;
                nvs["namespaceCount"] = nvs_stats.namespace_count;
            }

            // Get LittleFS partition info
            const esp_partition_t *fs_partition =
                esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
            if (fs_partition)
            {
                partitions["fsAddress"] = fs_partition->address;
                partitions["fsSize"] = fs_partition->size;
            }
        }
    } // namespace

    void WebServer::appendFirmware(JsonDocument &doc) const
    {
        // Firmware version
        JsonObject firmware = doc.createNestedObject("firmware");
        firmware["name"] = FIRMWARE_NAME;
        firmware["version"] = FIRMWARE_VERSION;
        firmware["buildDate"] = FIRMWARE_BUILD_DATE;
        firmware["buildTime"] = FIRMWARE_BUILD_TIME;
        firmware["product"] = FirmwareImage::PRODUCT;
        // "l2", or "other" when the partition table isn't RoofRoof's.
        firmware["layout"] = FirmwareMarker::layout();
    }

    void WebServer::appendRuntime(JsonDocument &doc) const
    {
        // System stats
        doc["uptime"] = millis() / 1000;
        doc["configRevision"] = configRevision.load();
        doc["freeHeap"] = ESP.getFreeHeap();
        // Stack headroom (bytes never used). This handler runs on the
        // AsyncTCP task, so "asyncTcp" is that task's own high-water mark.
        JsonObject stacks = doc.createNestedObject("stackFree");
        stacks["asyncTcp"] = uxTaskGetStackHighWaterMark(nullptr);
        stacks["loop"] = loopTaskHandle != nullptr ? uxTaskGetStackHighWaterMark(loopTaskHandle) : 0;
        stacks["roof"] = roof != nullptr ? roof->stackFreeBytes() : 0;
    }

    void WebServer::appendTime(JsonDocument &doc) const
    {
        // Current time: `epoch` in Unix seconds like every other timestamp
        // (0 until the clock is set, SQMeter spec 013 FR-003); `iso` is the local time
        // with its offset, for display.
        JsonObject timeObj = doc.createNestedObject("time");
        const time_t epochNow = time(nullptr);
        timeObj["epoch"] = epochNow >= Core::CLOCK_VALID_EPOCH ? static_cast<uint32_t>(epochNow) : 0U;
        if (timeManager)
        {
            timeObj["iso"] = timeManager->getCurrentTimeISO();
            timeObj["timezone"] = getConfigCallback().ntp.timezone;
        }
        else
        {
            time_t now;
            time(&now);
            struct tm timeinfo;
            localtime_r(&now, &timeinfo);

            char buffer[32];
            strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S%z", &timeinfo);
            timeObj["iso"] = buffer;
            timeObj["timezone"] = getConfigCallback().ntp.timezone;
        }

        // NTP status
        if (timeManager)
        {
            TimeStatus timeStatus = timeManager->getStatus();
            JsonObject ntp = doc.createNestedObject("ntp");
            ntp["enabled"] = timeStatus.ntpEnabled;
            ntp["synced"] = (timeStatus.syncStatus == NTPSyncStatus::SYNCED);
            ntp["status"] = static_cast<int>(timeStatus.syncStatus);
            ntp["lastSync"] = timeStatus.lastSyncMs;
            ntp["nextSync"] = timeStatus.nextSyncMs;
            ntp["drift"] = timeStatus.driftSeconds;
            ntp["server"] = timeStatus.server;
        }
    }

    void WebServer::appendWifi(JsonDocument &doc) const
    {
        // WiFi status
        JsonObject wifi = doc.createNestedObject("wifi");
        wifi["connected"] = WiFi.isConnected();
        wifi["ssid"] = WiFi.SSID();
        wifi["ip"] = WiFi.localIP().toString();
        wifi["rssi"] = WiFi.RSSI();
        wifi["mac"] = WiFi.macAddress();
        wifi["connectPending"] = wifiConnectActive.load();
        wifi["apMode"] = (WiFi.getMode() & WIFI_AP) != 0;
        {
            const Config &cfg = getConfigCallback();
            wifi["hostname"] = cfg.wifi.hostname;
            wifi["mdns"] = cfg.wifi.mdns;
        }
        Ipv6Network::appendStatus(wifi);
    }

    void WebServer::appendMqttStatus(JsonDocument &doc) const
    {
        if (!mqttClient)
            return;
        MQTTStatus mqttStatus = mqttClient->getStatus();
        JsonObject mqtt = doc.createNestedObject("mqtt");
        mqtt["enabled"] = mqttStatus.enabled;
        mqtt["connected"] = mqttStatus.connected;
        mqtt["state"] = mqttStatus.state;
        mqtt["lastPublish"] = mqttStatus.lastPublishMs;
        mqtt["lastReconnectAttempt"] = mqttStatus.lastReconnectAttemptMs;
        mqtt["broker"] = mqttStatus.broker; // std::string: copied into the doc (mqttStatus dies before serializing)
        mqtt["port"] = mqttStatus.port;
        mqtt["topic"] = mqttStatus.topic;
        mqtt["availabilityTopic"] = mqttStatus.availabilityTopic;
        mqtt["clientId"] = mqttStatus.clientId;
    }

    void WebServer::appendConnections(JsonDocument &doc) const
    {
        // Who holds the device's connections (SQMeter spec 011 FR-008, 013 FR-012).
        JsonObject connections = doc.createNestedObject("connections");
        connections["tcpLimit"] = ConnectionBudget::TCP_CONNECTIONS;
        JsonObject live = connections.createNestedObject("liveUpdates");
        live["status"] = wsStatus.count();
        live["limitPerEndpoint"] = ConnectionBudget::WEBSOCKETS_PER_ENDPOINT;
        live["replaced"] = wsReplaced;
        live["stalledClosed"] = wsStalled;
        connections["alpacaRestoredAfterRestart"] = alpacaConnectionsRestored;
    }

    std::string WebServer::createStatusJson() const
    {
        // Sized by test_status_document_fits.
        DynamicJsonDocument doc(Core::STATUS_DOC_CAPACITY);
        appendFirmware(doc);
        appendRuntime(doc);
        JsonObject alerts = doc.createNestedObject("alerts");
        Core::writeAlertSchedule(alerts, sharedSchedule(), getConfigCallback(), millis());
        alerts["recentRevision"] = alertDispatcher ? alertDispatcher->recentRevision() : 0;
        Core::writeClientWatch(
            doc.createNestedObject("alpaca"), sharedClientWatch(), alpacaBackend.devices(), getConfigCallback(), millis());
        appendMemory(doc);
        appendPartitions(doc);
        appendTime(doc);
        appendWifi(doc);
        appendMqttStatus(doc);
        appendConnections(doc);
        appendRoof(doc);
        // A full pool drops the trailing sections silently: say so.
        if (doc.overflowed())
            Logger::warn(TAG, "/api/status document overflowed %u bytes", static_cast<unsigned>(doc.capacity()));

        std::string json;
        serializeJson(doc, json);
        return json;
    }

} // namespace SQM
