#include <unity.h>

#include <ArduinoJson.h>

#include "DeviceCore.h"
#include "RoofStatus.h"
#include "SqmeterSite.h"
#include "SqmeterSync.h"

#include <cstdio>
#include <string>
#include <vector>

// Each /api/status section fits Core::STATUS_SECTION_CAPACITY in the worst case (SQMeter spec
// 030 FR-008, constitution VI): a full pool silently drops the trailing sections
// (diagnostics, MQTT, connections). The sections this library writes are
// written for real; the device's own (src/WebServerStatus.cpp) are modelled
// member for member with their longest strings. Measured on the host, where
// ArduinoJson's slots are bigger than on the ESP32, so the device has more room.

using namespace SQM;

namespace
{
    const std::string LONG(64, 'x'); // the longest saved settings strings (test_config_model)

    Config everythingOn()
    {
        Config c = Config::createDefault();
        c.alpaca.enabled = c.alerts.enabled = c.mqtt.enabled = true;
        c.ntp.timezone = LONG;
        c.wifi.hostname = std::string(32, 'h');
        return c;
    }

    // src/WebServerStatus.cpp appendFirmware, appendRuntime, appendMemory,
    // appendPartitions, appendTime, appendWifi (with Ipv6Network::appendStatus),
    // appendMqttStatus and appendConnections. A std::string or String value is
    // copied into the pool; a const char * is not.
    void deviceSections(JsonDocument &doc, const Config &cfg)
    {
        JsonObject firmware = doc.createNestedObject("firmware");
        for (const char *key : {"name", "product", "version", "buildDate", "buildTime", "layout"})
            firmware[key] = "literal";

        for (const char *key : {"uptime", "configRevision", "freeHeap"})
            doc[key] = 4294967295U;
        JsonObject stacks = doc.createNestedObject("stackFree");
        stacks["asyncTcp"] = stacks["loop"] = stacks["roof"] = 4096;

        JsonArray heapStages = doc.createNestedArray("heapStages");
        for (int i = 0; i < 16; ++i) // HeapTrace::MAX_CHECKPOINTS
        {
            JsonObject stage = heapStages.createNestedObject();
            stage["stage"] = "literal";
            stage["free"] = stage["largest"] = 4294967295U;
        }
        for (const char *key :
             {"minFreeHeap",
              "maxAllocHeap",
              "heapSize",
              "cpuFreqMHz",
              "flashSize",
              "sketchSize",
              "freeSketchSpace",
              "resetReason",
              "bootCount",
              "fsTotal",
              "fsUsed"})
            doc[key] = 4294967295U;

        JsonObject partitions = doc.createNestedObject("partitions");
        for (const char *key : {"runningSlot", "bootSlot", "nextSlot"})
            partitions[key] = "app0"; // esp_partition_t::label: a char array, referenced
        for (const char *key : {"runningAddress", "runningSize", "nextSize", "fsAddress", "fsSize"})
            partitions[key] = 4294967295U;
        JsonObject nvs = partitions.createNestedObject("nvs");
        for (const char *key : {"usedEntries", "freeEntries", "totalEntries", "namespaceCount"})
            nvs[key] = 4294967295U;

        JsonObject time = doc.createNestedObject("time");
        time["epoch"] = 4294967295U;
        time["iso"] = std::string("2026-10-17T23:59:59+14:00");
        time["timezone"] = cfg.ntp.timezone;
        JsonObject ntp = doc.createNestedObject("ntp");
        for (const char *key : {"enabled", "synced", "status", "lastSync", "nextSync", "drift"})
            ntp[key] = 4294967295U;
        ntp["server"] = LONG;

        JsonObject wifi = doc.createNestedObject("wifi");
        for (const char *key : {"connected", "rssi", "connectPending", "apMode", "mdns"})
            wifi[key] = -100;
        wifi["ssid"] = std::string(32, 's');
        wifi["ip"] = std::string("255.255.255.255");
        wifi["mac"] = std::string("AA:BB:CC:DD:EE:FF");
        wifi["hostname"] = cfg.wifi.hostname;
        JsonObject ipv6 = wifi.createNestedObject("ipv6");
        ipv6["enabled"] = true;
        JsonArray addresses = ipv6.createNestedArray("addresses");
        for (int i = 0; i < 3; ++i) // LWIP_IPV6_NUM_ADDRESSES
        {
            JsonObject entry = addresses.createNestedObject();
            entry["address"] = std::string("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff");
            entry["scope"] = "global";
        }

        // Spec 023: the Ethernet and pins blocks.
        JsonObject eth = doc.createNestedObject("ethernet");
        for (const char *key : {"present", "started", "phyFound", "link", "speedMbps", "fullDuplex"})
            eth[key] = 100;
        eth["ip"] = std::string("255.255.255.255");
        eth["mac"] = std::string("AA:BB:CC:DD:EE:FF");
        JsonArray ethIpv6 = eth.createNestedArray("ipv6");
        for (int i = 0; i < 3; ++i)
            ethIpv6.add(std::string("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"));
        JsonObject pins = doc.createNestedObject("pins");
        pins["preset"] = "wt32-carrier-v1";
        pins["board"] = "esp32-devkit";
        pins["valid"] = true;
        pins["restartNeeded"] = true;

        JsonObject mqtt = doc.createNestedObject("mqtt");
        for (const char *key : {"enabled", "connected", "state", "lastPublish", "lastReconnectAttempt", "port"})
            mqtt[key] = 4294967295U;
        mqtt["broker"] = LONG;
        mqtt["topic"] = LONG;
        mqtt["availabilityTopic"] = LONG + "/availability";
        mqtt["clientId"] = std::string("RoofRoof-AABBCC");

        JsonObject connections = doc.createNestedObject("connections");
        connections["tcpLimit"] = 16;
        JsonObject live = connections.createNestedObject("liveUpdates");
        for (const char *key : {"status", "limitPerEndpoint", "replaced", "stalledClosed"})
            live[key] = 4294967295U;
        connections["alpacaRestoredAfterRestart"] = true;
        JsonObject shortOfMemory = connections.createNestedObject("shortOfMemory"); // spec 031
        shortOfMemory["refused"] = shortOfMemory["skippedFrames"] = 4294967295U;

        // appendRoof (spec 003): every key filled, a fault with its longest text.
        RoofIo::Snapshot roof;
        roof.status.state = Roof::State::Error;
        roof.status.fault.kind = Roof::FaultKind::External;
        roof.status.fault.measured = 7.0f;
        roof.fault = RoofIo::externalRecord(RoofIo::FaultCause::ParkInterlock, 7.0f, 0.0f);
        roof.currentSense = true;
        roof.motorAmps = 12.34f;
        roof.supplyVolts = 12.34f;
        roof.fittedSwitches = 2;
        roof.maxWatchdogBeatAgeMs = 4294967295U;
        // Spec 008: the longest phase name and the capped check texts.
        roof.closePhase = "heldByPerson";
        roof.weatherText = std::string(RoofIo::MAX_CHECK_TEXT, 'w');
        roof.parkText = std::string(RoofIo::MAX_CHECK_TEXT, 'p');
        // Spec 022: every bypass in effect, the longest text.
        roof.testBox.set = roof.testBox.park = roof.testBox.weather = roof.testBox.daylight = true;
        roof.testBox.until = 4294967295U;
        roof.needsPerson = {true, std::string(RoofIo::MAX_CHECK_TEXT, 'n')}; // spec 030
        JsonObject roofOut = doc.createNestedObject("roof");
        RoofIo::writeStatus(roofOut, roof);
        RoofIo::writeDaylight(roofOut, roof);
        RoofIo::writeCloseRearm(roofOut, roof);
        RoofIo::writeMoveGuard(roofOut, roof);
        RoofIo::writeHealth(roofOut, roof, true);
    }

    // Every member and element takes one slot.
    size_t slots(JsonVariantConst value)
    {
        size_t count = 0;
        if (value.is<JsonObjectConst>())
            for (JsonPairConst member : value.as<JsonObjectConst>())
                count += 1 + slots(member.value());
        else if (value.is<JsonArrayConst>())
            for (JsonVariantConst element : value.as<JsonArrayConst>())
                count += 1 + slots(element);
        return count;
    }

    // ArduinoJson's slot is 16 bytes on the ESP32 (32-bit pointers) and bigger
    // on the host: what the same document takes on the device.
    constexpr size_t DEVICE_SLOT_BYTES = 16;
    size_t onDevice(const JsonDocument &doc)
    {
        const size_t hostSlot = JSON_OBJECT_SIZE(1);
        return doc.memoryUsage() - slots(doc.as<JsonVariantConst>()) * (hostSlot - DEVICE_SLOT_BYTES);
    }

    // The library's sections, as createStatusJson writes them.
    void coreSections(JsonDocument &doc, const Config &cfg, uint32_t now)
    {
        JsonObject alerts = doc.createNestedObject("alerts");
        Alerts::ScheduleState schedule;
        schedule.sending = false;
        schedule.reason = Alerts::ScheduleReason::WaitingForClient;
        schedule.sinceKnown = true;
        schedule.sinceEpoch = 1791417600;
        Core::writeAlertSchedule(alerts, schedule, cfg, now);
        alerts["recentRevision"] = 4294967295U;
        const std::vector<Alpaca::DeviceInfo> devices(Alpaca::MAX_DEVICES, Alpaca::DeviceInfo{"Dome", "dome", 0, LONG, LONG, 3});
        Core::writeClientWatch(doc.createNestedObject("alpaca"), Alpaca::ClientWatch{}, devices, cfg, now);
        // Spec 020: the location section, every field filled.
        LocationConfig location;
        location.set = location.gps = true;
        location.savedAt = 4294967295U;
        Site::LocationRead read;
        read.problem = Site::Problem::NoLocation;
        read.goodAtMs = 1;
        Site::writeLocationStatus(doc.createNestedObject("location"), location, read, true, now);
        // Spec 020: the sync section, every group refused and needing its secrets.
        Config synced = cfg;
        synced.sqmeterSync.time.syncedAt = synced.sqmeterSync.mqtt.syncedAt = synced.sqmeterSync.alerts.syncedAt = 4294967295U;
        Site::ConfigRead settings;
        settings.problem = Site::Problem::Unreadable;
        settings.goodAtMs = 1;
        settings.site.hasMqtt = settings.site.hasNtfy = settings.site.passwordSet = settings.site.ntfyTokenSet = true;
        Site::SyncState state;
        for (std::string &refused : state.refused)
            refused = "SQMeter's value refused: " + LONG;
        Site::writeSyncStatus(doc.createNestedObject("sync"), Site::SyncView{synced, settings, state, true, now});
    }
} // namespace

// Spec 031: the sections writeStatus writes one after another from one pool,
// by their top-level keys, in document order. Every key is in one section.
namespace
{
    const std::vector<std::vector<const char *>> SECTIONS = {
        {"firmware", "uptime", "configRevision", "freeHeap", "stackFree"},
        {"alerts", "alpaca"},
        {"heapStages",
         "minFreeHeap",
         "maxAllocHeap",
         "heapSize",
         "cpuFreqMHz",
         "flashSize",
         "sketchSize",
         "freeSketchSpace",
         "resetReason",
         "bootCount",
         "fsTotal",
         "fsUsed",
         "partitions",
         "time",
         "ntp"},
        {"wifi", "ethernet", "pins"},
        {"mqtt", "connections"},
        {"roof"},
        {"location", "sync"},
    };
} // namespace

void test_status_document_fits()
{
    const uint32_t now = 100000;
    const Config cfg = everythingOn();
    DynamicJsonDocument doc(16384);
    deviceSections(doc, cfg);
    coreSections(doc, cfg, now);
    TEST_ASSERT_FALSE(doc.overflowed());
    TEST_ASSERT_TRUE(doc["alpaca"]["clients"].containsKey("dome"));

    size_t keys = 0;
    for (const auto &section : SECTIONS)
    {
        DynamicJsonDocument part(Core::STATUS_SECTION_CAPACITY * 4);
        for (const char *key : section)
        {
            TEST_ASSERT_TRUE_MESSAGE(doc.containsKey(key), key);
            part[key] = doc[key];
            ++keys;
        }
        TEST_ASSERT_FALSE(part.overflowed());
        // With 20% headroom, like SQMeter's readings document.
        const size_t used = onDevice(part);
        char detail[96];
        snprintf(
            detail,
            sizeof detail,
            "%s section: pool %u bytes of %u",
            section.front(),
            static_cast<unsigned>(used),
            static_cast<unsigned>(Core::STATUS_SECTION_CAPACITY));
        TEST_ASSERT_TRUE_MESSAGE(used * 12 / 10 <= Core::STATUS_SECTION_CAPACITY, detail);
        printf("status document on the device: %s\n", detail);
    }
    // No top-level key is left out of a section.
    TEST_ASSERT_EQUAL_UINT(doc.as<JsonObjectConst>().size(), keys);
}
