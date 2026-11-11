// EmulatedDevice: the roof's REST commands, the Demo panel and the roof's
// part of the status and the saved state (spec 011, contracts/demo-roof.md).

#include "emulated_device.h"

#include "CalibrationDocument.h"
#include "PlatformHold.h"
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
    doc["daylight"] = Daylight::describe(roof.daylight());
    const DemoRoof::Mount &m = roof.mountState();
    doc["mount"] = m.slewing ? "slewing" : (m.atPark ? "parked" : "unparked");
    doc["state"] = Roof::stateName(roof.snapshot().status.state);
    doc["weather"] = Safety::describe(roof.weather());
    doc["park"] = Safety::describe(roof.parkResult());
    doc["phase"] = CloseSeq::phaseName(roof.phase());
    return serialize(doc);
}
