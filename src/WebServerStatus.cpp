#include "BenchMode.h"
#include "ConnectionBudget.h"
#include "FirmwareMarker.h"
#include "HeapReserve.h"
#include "HeapTrace.h"
#include "HeapTrend.h"
#include "Ipv6Network.h"
#include "Logger.h"
#include "RoofControl.h"
#include "RoofMqtt.h"
#include "WebServer.h"
#include "JsonStream.h"
#include <esp_heap_caps.h>
#include "WebReply.h"
#include "version.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>

#include "EthernetLink.h"
#include "NetState.h"
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
            // Spec 027: announced whenever discovery is on (it pauses and arms
            // alerts; with the master switch off nothing is sent either way).
            groups.alertsSwitch = true;
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
    // Spec 048: one config at a time (HaDiscovery::Pacer), and nothing again
    // after a reconnect to a broker that kept this boot's set.
    void WebServer::publishDiscovery()
    {
        if (mqttClient == nullptr || !mqttClient->isConnected() || roofBusyForMqtt())
            return;
        const uint32_t connection = mqttClient->connectionCount();
        const uint32_t revision = configRevision.load();
        if (connection != discoveryConnection || revision != discoveryRevision)
            restartDiscovery(connection, revision);
        stepDiscovery();
    }

    void WebServer::restartDiscovery(uint32_t connection, uint32_t revision)
    {
        const Config &cfg = getConfigCallback();
        const MQTTConfig &mqtt = cfg.mqtt;
        const HaDiscovery::Groups groups = discoveryGroups(cfg);
        char mac[13];
        snprintf(mac, sizeof(mac), "%012llx", static_cast<unsigned long long>(ESP.getEfuseMac()));
        HaDiscovery::Device device{
            std::string("roofroof_") + mac,
            cfg.deviceName,
            std::string(FIRMWARE_VERSION) + (Bench::BUILD ? " (bench)" : ""),
            mqtt.topic,
            mqtt.discoveryPrefix};
        const std::string key = discoveryKeyOf(mqtt.homeAssistant, device, groups);
        const bool sameConnection = connection == discoveryConnection;
        discoveryConnection = connection;
        discoveryRevision = revision;
        if (key == discoveryKey && sameConnection)
            return; // a save that changed nothing discovery depends on

        if (!discoveryKey.empty() && discoveryWasOn &&
            (!mqtt.homeAssistant || discoveryDevice.prefix != device.prefix || discoveryDevice.baseTopic != device.baseTopic))
        {
            // Remove what was announced under the old settings (a person's
            // save, not a return: at once, an empty retained payload each).
            HaDiscovery::forEach(
                discoveryDevice,
                groups,
                [this](const std::string &topic, const JsonDocument *) { mqttClient->publishTopic(topic, std::string(), true); });
        }
        discoveryKey = key;
        discoveryDevice = device;
        discoveryGroupsNow = groups;
        discoveryWasOn = mqtt.homeAssistant;
        discoveryMarker = HaDiscovery::markerOf(key);
        if (!mqtt.homeAssistant)
        {
            discoveryPacer = HaDiscovery::Pacer{};
            return;
        }
        // The broker's marker is trusted only for a set this boot sent whole.
        discoveryPacer.start(discoveryMarker, millis(), discoveryMarker == discoveryPublished);
    }

    void WebServer::stepDiscovery()
    {
        if (!discoveryPacer.running() || !mqttClient->mayWrite())
            return;
        // Spec 048: not into a heap a network return has already cut up.
        const uint32_t largest = static_cast<uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        if (!ConnectionBudget::backgroundFits(largest, NetState::phase() != ConnectionBudget::ReturnPhase::Settled))
            return;
        const uint32_t now = millis();
        const HaDiscovery::Pacer::Step step = discoveryPacer.next(now, HaDiscovery::count());
        bool done = true;
        switch (step.kind)
        {
        case HaDiscovery::Pacer::Kind::Entity:
        {
            // Spec 031: streamed from the document's pool; an empty payload removes.
            DynamicJsonDocument doc(HaDiscovery::CONFIG_DOC_BYTES);
            HaDiscovery::publishOne(
                step.index,
                discoveryDevice,
                discoveryGroupsNow,
                doc,
                [this, &done](const std::string &topic, const JsonDocument *config)
                {
                    done = config != nullptr ? mqttClient->publishJson(topic, *config, true)
                                             : mqttClient->publishTopic(topic, std::string(), true);
                });
            break;
        }
        case HaDiscovery::Pacer::Kind::Marker:
        {
            char text[HaDiscovery::MARKER_TEXT_BYTES];
            HaDiscovery::markerText(discoveryMarker, text);
            done = mqttClient->publishSubtopicBytes(HaDiscovery::MARKER_SUBTOPIC, text, sizeof(text) - 1, true);
            if (done)
            {
                discoveryPublished = discoveryMarker;
                NetState::noteDiscovery(ConnectionBudget::DiscoveryOutcome::Published);
            }
            break;
        }
        case HaDiscovery::Pacer::Kind::Skip:
            Logger::info(TAG, "Home Assistant discovery: the broker kept it, nothing sent again");
            NetState::noteDiscovery(ConnectionBudget::DiscoveryOutcome::Skipped);
            break;
        case HaDiscovery::Pacer::Kind::Wait:
        case HaDiscovery::Pacer::Kind::Done:
            break;
        }
        // A config that didn't go out is tried again after the gap.
        if (done)
            discoveryPacer.done(step, now);
    }

    // Spec 048: the broker's retained marker, handed to the pacer.
    void WebServer::subscribeDiscoveryMarker()
    {
        if (mqttClient == nullptr)
            return;
        mqttClient->onCommand(
            HaDiscovery::MARKER_SUBTOPIC,
            [this](const std::string &payload, RoofIo::Mqtt::Delivery)
            { discoveryPacer.markerSeen(HaDiscovery::parseMarker(payload.data(), payload.size())); });
    }

    // Spec 035: no allocation (the Alpaca device list is a copy, and the
    // alert channels don't depend on it).
    Deps::Facts WebServer::networkFacts() const
    {
        Deps::Facts facts;
        facts.wifiConnected = NetState::online(); // spec 023: Ethernet counts
        facts.mqttConnected = mqttClient != nullptr && mqttClient->isConnected();
        facts.clockSet = static_cast<int64_t>(time(nullptr)) >= Core::CLOCK_VALID_EPOCH;
        return facts;
    }

    Deps::Facts WebServer::settingsFacts() const
    {
        Deps::Facts facts = networkFacts();
        facts.alpacaDevices = static_cast<uint8_t>(alpacaBackend.devices().size());
        return facts;
    }

    // Spec 035: asked for every alert sent, so it allocates nothing (it built
    // every setting's entry list on the heap; a burst of alerts aborted there).
    ChannelBlocks WebServer::channelBlocks() const
    {
        Deps::Entry entries[Deps::ALERT_CHANNEL_ENTRIES];
        const size_t count = Deps::alertChannels(getConfigCallback(), networkFacts(), entries);
        uint8_t passing = 0;
        auto blocked = [&entries, count, &passing](const char *setting, AlertChannel channel) -> const char *
        {
            const Deps::Reason *reason = Deps::channelReason(entries, count, setting);
            if (reason == nullptr || std::strcmp(reason->code, "alerts-off") == 0)
                return nullptr;
            // Spec 030: the network or the broker coming back lets a resend through.
            if (std::strcmp(reason->code, "wifi-disconnected") == 0 || std::strcmp(reason->code, "mqtt-disconnected") == 0)
                passing |= alertChannelBit(channel);
            return reason->text;
        };
        ChannelBlocks blocks{};
        blocks[static_cast<size_t>(AlertChannel::Mqtt)] = blocked("alerts.mqtt.enabled", AlertChannel::Mqtt);             // dep: D-01 D-02
        blocks[static_cast<size_t>(AlertChannel::Pushover)] = blocked("alerts.pushover.enabled", AlertChannel::Pushover); // dep: D-03
        blocks[static_cast<size_t>(AlertChannel::Ntfy)] = blocked("alerts.ntfy.enabled", AlertChannel::Ntfy);
        blocks[static_cast<size_t>(AlertChannel::Webhook)] = blocked("alerts.webhook.enabled", AlertChannel::Webhook);
        blocks.forNow = passing;
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
            // Spec 037: the heap ordinary allocations use. freeHeap and
            // maxAllocHeap also count the IRAM heap (32-bit access only, about
            // 59 KB here), which malloc and new never use.
            JsonObject heap8 = doc.createNestedObject("heap8Bit");
            heap8["free"] = heap_caps_get_free_size(MALLOC_CAP_8BIT);
            heap8["largest"] = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
            heap8["minFree"] = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
            heap8["reserveSpent"] = HeapSafe::reserve().spent(); // spec 038
            // Spec 045: the lowest largest block over the last 15 and 60
            // minutes (one reading a second; ten while a network return is
            // followed, spec 048): fragmentation over a session.
            heap8["largestLow15m"] = HeapSafe::trend().lowestLargest(15);
            heap8["largestLow60m"] = HeapSafe::trend().lowestLargest(60);
            doc["heapSize"] = ESP.getHeapSize();
            doc["cpuFreqMHz"] = ESP.getCpuFreqMHz();
            doc["flashSize"] = ESP.getFlashChipSize();
            // getSketchSize() maps and verifies the whole app image (about a
            // second, and two at once fail with "bootloader_mmap twice"); the
            // image can't change while running, so read it once.
            static const uint32_t sketchSize = ESP.getSketchSize();
            static const uint32_t freeSketchSpace = ESP.getFreeSketchSpace();
            doc["sketchSize"] = sketchSize;
            doc["freeSketchSpace"] = freeSketchSpace;
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
        live["refused"] = wsRefused;
        live["stalledClosed"] = wsStalled;
        connections["alpacaRestoredAfterRestart"] = alpacaConnectionsRestored;
        // Spec 031: requests answered "busy" and live frames skipped for memory.
        JsonObject shortOfMemory = connections.createNestedObject("shortOfMemory");
        shortOfMemory["refused"] = WebReply::refused();
        shortOfMemory["skippedFrames"] = WebReply::skippedFrames();
    }

    // Spec 031: written a section at a time from one small pool (research R4),
    // in the order the document always had; each section's size is pinned by
    // test_status_document_fits.
    void WebServer::writeStatus(HeapSafe::ChunkText &out) const
    {
        DynamicJsonDocument doc(Core::STATUS_SECTION_CAPACITY);
        writeStatus(out, doc);
    }

    void WebServer::writeStatus(HeapSafe::ChunkText &out, JsonDocument &doc) const
    {
        HeapSafe::JsonStream json(out);
        const auto section = [&doc, &json](auto write)
        {
            doc.clear();
            write();
            json.members(doc);
        };
        json.beginObject();
        section(
            [&]()
            {
                appendFirmware(doc);
                appendRuntime(doc);
            });
        section(
            [&]()
            {
                JsonObject alerts = doc.createNestedObject("alerts");
                Core::writeAlertSchedule(alerts, sharedSchedule(), getConfigCallback(), millis());
                alerts["recentRevision"] = alertDispatcher ? alertDispatcher->recentRevision() : 0;
                Core::writeClientWatch(
                    doc.createNestedObject("alpaca"), sharedClientWatch(), alpacaBackend.devices(), getConfigCallback(), millis());
            });
        section(
            [&]()
            {
                appendMemory(doc);
                appendPartitions(doc);
                appendTime(doc);
            });
        section(
            [&]()
            {
                appendWifi(doc);
                EthernetLink::appendStatus(doc);
                appendPinsStatus(doc);
            }); // spec 023
        section(
            [&]()
            {
                appendMqttStatus(doc);
                appendConnections(doc);
            });
        section([&]() { appendRoof(doc); });
        section([&]() { appendSite(doc); });
        json.endObject();
        // A full pool drops a section's trailing members silently: say so.
        if (json.overflowed())
            Logger::warn(TAG, "/api/status section overflowed its %u-byte pool", static_cast<unsigned>(doc.capacity()));
    }

} // namespace SQM
