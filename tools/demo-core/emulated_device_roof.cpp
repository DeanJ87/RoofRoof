// EmulatedDevice: the roof's REST commands, the Demo panel and the roof's
// part of the status and the saved state (spec 011, contracts/demo-roof.md).

#include "emulated_device.h"

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
        roof.setInjectors(next);
        const std::string mount = p["mount"] | "";
        if (mount == "park")
            roof.parkMount();
        else if (mount == "unpark")
            roof.unparkMount();
    }
    return roofPanel();
}

std::string EmulatedDevice::platformHold() const
{
    const Roof::State state = roof.snapshot().status.state;
    if (state == Roof::State::Opening || state == Roof::State::Closing)
        return RoofIo::holdText(RoofIo::HoldReason::Moving);
    return "";
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
    const DemoRoof::Mount &m = roof.mountState();
    doc["mount"] = m.slewing ? "slewing" : (m.atPark ? "parked" : "unparked");
    doc["state"] = Roof::stateName(roof.snapshot().status.state);
    doc["weather"] = Safety::describe(roof.weather());
    doc["park"] = Safety::describe(roof.parkResult());
    doc["phase"] = CloseSeq::phaseName(roof.phase());
    return serialize(doc);
}
