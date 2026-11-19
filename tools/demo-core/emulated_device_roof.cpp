// EmulatedDevice: the roof's REST commands, the Demo panel and the roof's
// part of the status and the saved state (spec 011, contracts/demo-roof.md).

#include "emulated_device.h"

#include "CalibrationDocument.h"
#include "PlatformHold.h"
#include "Teach.h"
#include "RoofStatus.h"

using namespace DemoCore;

namespace
{
    // The device's sendOutcome (src/WebServerRoof.cpp): 200 with the state, or
    // 409 with the refusal's reason and text.
    std::string outcomeBody(const DemoRoof::Outcome &outcome)
    {
        StaticJsonDocument<512> doc;
        doc["success"] = outcome.accepted;
        doc["state"] = Roof::stateName(outcome.state);
        if (!outcome.accepted)
        {
            doc["reason"] = outcome.reason;
            doc["error"] = outcome.text;
        }
        std::string body;
        serializeJson(doc, body);
        return body;
    }
} // namespace

std::string EmulatedDevice::roofCommand(const std::string &name)
{
    runRoof(); // what the device sees now
    DemoRoof::Outcome outcome;
    if (name == "open")
        outcome = roof.command(Roof::Command::Open, Roof::Origin::Person);
    else if (name == "close")
        outcome = roof.command(Roof::Command::Close, Roof::Origin::Person);
    else if (name == "stop")
        outcome = roof.command(Roof::Command::Stop, Roof::Origin::Person);
    else if (name == "clear-fault")
        outcome = roof.clearFault();
    else
        return response(404, errorJson("Not found"));
    return response(outcome.accepted ? 200 : 409, outcomeBody(outcome));
}

std::string EmulatedDevice::calibration(const std::string &action, const std::string &body)
{
    runRoof();
    if (action == "status")
    {
        DynamicJsonDocument doc(Calib::DOCUMENT_CAPACITY);
        Calib::writeDocument(doc.to<JsonObject>(), roof.calibrationReport(), roof.clockMs());
        return response(200, serialize(doc));
    }
    if (action == "abort")
    {
        const DemoRoof::Outcome outcome = roof.command(Roof::Command::Stop, Roof::Origin::Person);
        return response(outcome.accepted ? 200 : 409, outcomeBody(outcome));
    }
    if (action == "confirm")
    {
        StaticJsonDocument<128> in;
        if (deserializeJson(in, body) || !in["atOpenEnd"].is<bool>())
            return response(400, errorJson("atOpenEnd must be true or false"));
        const DemoRoof::Outcome outcome = roof.confirmCalibration(in["atOpenEnd"].as<bool>());
        return response(outcome.accepted ? 200 : 409, outcomeBody(outcome));
    }
    if (action != "start")
        return response(404, errorJson("Not found"));
    StaticJsonDocument<512> in;
    Calib::Request wanted;
    const char *error = "The body must be JSON";
    if (deserializeJson(in, body) || !Calib::parseRequest(in.as<JsonVariantConst>(), wanted, error))
        return response(400, errorJson(error));
    const DemoRoof::Outcome outcome = roof.startCalibration(wanted);
    StaticJsonDocument<512> doc;
    doc["success"] = outcome.accepted;
    doc["state"] = outcome.accepted ? "running" : Roof::stateName(outcome.state);
    if (!outcome.accepted)
    {
        doc["reason"] = outcome.reason;
        doc["error"] = outcome.text;
    }
    return response(outcome.accepted ? 200 : 409, serialize(doc));
}

namespace
{
    // The device's sendAnswer (src/WebServerTeach.cpp).
    std::string teachBody(const DemoRoof::Outcome &outcome, uint32_t jogId = 0, uint32_t limitMs = 0)
    {
        StaticJsonDocument<512> doc;
        doc["success"] = outcome.accepted;
        doc["state"] = Roof::stateName(outcome.state);
        if (jogId != 0 && outcome.accepted)
        {
            doc["id"] = jogId;
            doc["holdMs"] = Roof::JOG_HOLD_MS;
            doc["keepAliveMs"] = Roof::JOG_KEEPALIVE_MS;
            doc["limitMs"] = limitMs;
        }
        if (!outcome.accepted)
        {
            doc["reason"] = outcome.reason;
            doc["error"] = outcome.text;
        }
        std::string body;
        serializeJson(doc, body);
        return body;
    }
} // namespace

// Spec 019: WebServer::handleTeachConfirm: the proposal checked, then saved as a settings save.
std::string EmulatedDevice::teachConfirm(const std::string &body)
{
    StaticJsonDocument<256> in;
    Calib::TeachValues values;
    const char *error = "The body must be JSON";
    if (deserializeJson(in, body) || !Calib::parseConfirm(in.as<JsonVariantConst>(), values, error))
        return response(400, errorJson(error));
    const DemoRoof::Outcome check = roof.teachConfirmable(values);
    if (!check.accepted)
        return response(409, teachBody(check));
    const std::string held = platformHold("flashWrite");
    if (!held.empty())
        return response(409, errorJson(held));
    Config next = cfg;
    next.roof.hallHighEnd = values.highEnd == Roof::End::Open ? RoofConfig::HallEnd::Open : RoofConfig::HallEnd::Closed;
    next.roof.hallHighCounts = values.highCounts;
    next.roof.hallLowCounts = values.lowCounts;
    std::string reason;
    if (!next.validate(&reason))
        return response(400, errorJson(reason));
    cfg = next;
    roof.settingsChanged(cfg);
    roof.teachClear();
    return response(200, "{\"success\":true}");
}

std::string EmulatedDevice::teach(const std::string &action, const std::string &body)
{
    runRoof();
    if (action == "status")
    {
        DynamicJsonDocument doc(Calib::TEACH_DOCUMENT_CAPACITY);
        Calib::writeTeachDocument(doc.to<JsonObject>(), roof.teachReport());
        return response(200, serialize(doc));
    }
    if (action == "discard")
    {
        roof.teachClear();
        return response(200, "{\"success\":true}");
    }
    if (action == "confirm")
        return teachConfirm(body);
    StaticJsonDocument<256> in;
    const char *error = "The body must be JSON";
    if (action == "mark")
    {
        Calib::Where at = Calib::Where::Closed;
        if (deserializeJson(in, body) || !Calib::parseMark(in.as<JsonVariantConst>(), at, error))
            return response(400, errorJson(error));
        const DemoRoof::Outcome outcome = roof.teachMark(at);
        return response(outcome.accepted ? 200 : 409, teachBody(outcome));
    }
    if (action != "jog")
        return response(404, errorJson("Not found"));
    Calib::JogRequest wanted;
    if (deserializeJson(in, body) || !Calib::parseJog(in.as<JsonVariantConst>(), wanted, error))
        return response(400, errorJson(error));
    uint32_t id = 0;
    uint32_t limitMs = 0;
    const DemoRoof::Outcome outcome = roof.jog(wanted, id, limitMs);
    return response(outcome.accepted ? 200 : 409, teachBody(outcome, id, limitMs));
}

std::string EmulatedDevice::roofInject(const std::string &json)
{
    StaticJsonDocument<512> p;
    if (!deserializeJson(p, json))
    {
        DemoRoof::Injectors next = roof.injectors();
        next.unsafe = p["unsafe"] | next.unsafe;
        next.rain = p["rain"] | next.rain;
        next.parkFails = p["parkFails"] | next.parkFails;
        next.jam = p["jam"] | next.jam;
        next.supplyLow = p["supplyLow"] | next.supplyLow;
        next.sunUp = p["sunUp"] | next.sunUp;
        next.sqmeterOffline = p["sqmeterOffline"] | next.sqmeterOffline;
        roof.setInjectors(next);
        const std::string mount = p["mount"] | "";
        if (mount == "park")
            roof.parkMount();
        else if (mount == "unpark")
            roof.unparkMount();
    }
    return roofPanel();
}

std::string EmulatedDevice::platformHold(const std::string &action) const
{
    RoofIo::PlatformAction which = RoofIo::PlatformAction::FlashWrite;
    if (action == "firmwareUpload")
        which = RoofIo::PlatformAction::FirmwareUpload;
    else if (action == "filesystemUpload")
        which = RoofIo::PlatformAction::FilesystemUpload;
    else if (action == "restart")
        which = RoofIo::PlatformAction::Restart;
    RoofIo::PlatformState state;
    state.roof = roof.snapshot().status.state;
    state.sequence = CloseSeq::platformState(roof.phase());
    state.calibrating = roof.calibrating();
    const RoofIo::HoldReason reason = RoofIo::platformHold(which, state);
    return reason == RoofIo::HoldReason::None ? std::string() : RoofIo::holdText(reason);
}

std::string EmulatedDevice::roofPanel() const
{
    StaticJsonDocument<1536> doc;
    const DemoRoof::Injectors &i = roof.injectors();
    doc["unsafe"] = i.unsafe;
    doc["rain"] = i.rain;
    doc["parkFails"] = i.parkFails;
    doc["jam"] = i.jam;
    doc["supplyLow"] = i.supplyLow;
    doc["sunUp"] = i.sunUp;
    doc["sqmeterOffline"] = i.sqmeterOffline;
    doc["daylight"] = Daylight::describe(roof.daylight());
    const DemoRoof::Mount &m = roof.mountState();
    doc["mount"] = m.slewing ? "slewing" : (m.atPark ? "parked" : "unparked");
    doc["state"] = Roof::stateName(roof.snapshot().status.state);
    doc["weather"] = Safety::describe(roof.weather());
    doc["park"] = Safety::describe(roof.parkResult());
    doc["phase"] = CloseSeq::phaseName(roof.phase());
    return serialize(doc);
}
