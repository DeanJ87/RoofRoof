// EmulatedDevice: the documents it serves and its saved state (see emulated_device.h).

#include "emulated_device.h"

#include "RoofStatus.h"

using namespace DemoCore;

std::string EmulatedDevice::statusParts() const
{
    DynamicJsonDocument doc(6144);
    doc["uptime"] = (nowMs - bootMs) / 1000;
    Core::writeAlertSchedule(doc.createNestedObject("alerts"), schedule.state(), cfg, nowMs);
    Core::writeClientWatch(doc.createNestedObject("alpaca"), clientWatch, devices(), cfg, nowMs);
    RoofIo::writeStatus(doc.createNestedObject("roof"), roof.snapshot()); // the firmware's own writer (spec 011)
    return serialize(doc);
}

std::string EmulatedDevice::effective() const
{
    const Deps::Facts f = facts();
    const std::vector<Deps::Entry> entries = Deps::evaluate(cfg, f);
    DynamicJsonDocument doc(Deps::reportCapacity(entries));
    Deps::writeReport(doc.to<JsonObject>(), entries, f);
    return serialize(doc);
}

std::string EmulatedDevice::pending() const
{
    DynamicJsonDocument doc(1024);
    JsonArray alerts = doc.createNestedArray("alerts");
    if (cfg.alerts.enabled)
        for (const Alerts::Wait &w : engine.waits(uptimeSeconds(), Core::alertRules(cfg)))
        {
            JsonObject item = alerts.createNestedObject();
            item["condition"] = w.condition;
            item["kind"] = Alerts::waitKindName(w.kind);
            item["remainingSeconds"] = w.remainingSeconds;
        }
    return serialize(doc);
}

std::string EmulatedDevice::recentAlerts() const
{
    DynamicJsonDocument doc(16384);
    doc["enabled"] = cfg.alerts.enabled;
    doc["armed"] = schedule.state().sending;
    JsonArray arr = doc.createNestedArray("alerts");
    for (auto it = records.rbegin(); it != records.rend(); ++it)
    {
        JsonObject item = arr.createNestedObject();
        item["id"] = it->id;
        item["event"] = Alerts::alertTypeName(it->alert.type);
        item["title"] = it->alert.title;
        item["message"] = it->alert.message;
        item["level"] = Alerts::alertLevelName(it->alert.level);
        item["ageSeconds"] = uptimeSeconds() - it->uptimeSeconds;
        if (it->epochSeconds != 0)
            item["timestamp"] = it->epochSeconds;
        JsonObject channels = item.createNestedObject("channels");
        for (size_t i = 0; i < 4; ++i)
        {
            if (it->skipped[i] != nullptr)
            {
                JsonObject ch = channels.createNestedObject(CHANNEL_NAMES[i]);
                ch["status"] = "skipped";
                ch["detail"] = it->skipped[i];
                continue;
            }
            if (!it->channel[i])
                continue;
            JsonObject ch = channels.createNestedObject(CHANNEL_NAMES[i]);
            ch["status"] = "sent";
            ch["detail"] = DEMO_DETAIL;
        }
    }
    return serialize(doc);
}

std::string EmulatedDevice::armedDocument() const
{
    StaticJsonDocument<384> doc;
    Core::writeAlertSchedule(doc.to<JsonObject>(), schedule.state(), cfg, nowMs);
    return serialize(doc);
}

std::string EmulatedDevice::deliveryRequests(uint32_t id, const std::string &credentialsJson) const
{
    const Record *found = nullptr;
    for (const Record &r : records)
        if (r.id == id)
            found = &r;
    DynamicJsonDocument out(4096);
    out.to<JsonObject>();
    StaticJsonDocument<1024> creds;
    if (found == nullptr || deserializeJson(creds, credentialsJson))
        return serialize(out);

    Alerts::Alert alert = found->alert;
    if (alert.level == Alerts::AlertLevel::Wake)
        alert.level = Alerts::AlertLevel::Urgent;
    const std::string &device = cfg.deviceName;
    auto wanted = [&](uint8_t bit, const char *name) { return (found->requested & bit) != 0 && creds.containsKey(name); };

    if (wanted(0x01, "mqtt"))
    {
        JsonObject mqtt = out.createNestedObject("mqtt");
        mqtt["topic"] = std::string(creds["mqtt"]["topic"] | "roofroof") + "/alerts";
        mqtt["payload"] = Delivery::alertJson(alert, device, found->epochSeconds);
    }
    if (wanted(0x02, "pushover"))
        writeRequest(
            out.createNestedObject("pushover"),
            Delivery::pushoverRequest(
                alert, {creds["pushover"]["userKey"] | "", creds["pushover"]["appToken"] | "", cfg.alerts.pushoverSound}, device));
    if (wanted(0x04, "ntfy"))
        writeRequest(
            out.createNestedObject("ntfy"),
            Delivery::ntfyRequest(alert, {"https://ntfy.sh", creds["ntfy"]["topic"] | "", creds["ntfy"]["token"] | ""}, device));
    return serialize(out);
}

std::string EmulatedDevice::saveState() const
{
    DynamicJsonDocument doc(8192);
    doc["config"] = serialized(cfg.toJson(false));
    const Alerts::ScheduleState &state = schedule.state();
    doc["armed"] = state.sending;
    doc["armedReason"] = static_cast<uint8_t>(state.reason);
    doc["armedSince"] = static_cast<double>(state.sinceEpoch);
    roof.save(doc.createNestedObject("roof"));
    return serialize(doc);
}

bool EmulatedDevice::loadState(const std::string &json)
{
    DynamicJsonDocument doc(json.size() * 2 + 4096);
    if (deserializeJson(doc, json))
        return false;
    std::string configJson;
    serializeJson(doc["config"], configJson);
    if (!loadConfig(configJson))
        return false;
    schedule = Alerts::AlertSchedule{};
    schedule.restore(
        doc["armed"] | true, doc.containsKey("armedReason"), doc["armedReason"] | 0, static_cast<int64_t>(doc["armedSince"] | 0.0));
    roof.load(doc["roof"]); // a reload is a restart: the roof boots where it was
    roofBooted = false;
    return true;
}
