// EmulatedDevice: advanced diagnostics (spec 017), the firmware's Diagnostics
// and WebServerDiag on the simulated roof, through the same lib/DiagLogic.

#include "emulated_device.h"

#include "DiagDocuments.h"
#include "PlatformHold.h"
#include "RoofReadings.h"

void EmulatedDevice::syncDiag()
{
    if (cfg.diag.enabled && !diagLog)
    {
        diagLog = std::make_unique<Diag::MoveLog>();
        if (!diagList)
            diagList = std::make_unique<Diag::SummaryList>();
        diagLog->setNextId(diagList->highestId() + 1);
        diagAddedId = diagList->highestId();
        roof.attachDiag(diagLog.get());
    }
    else if (!cfg.diag.enabled && diagLog)
    {
        roof.attachDiag(nullptr);
        diagLog.reset();
        diagList.reset();
    }
}

void EmulatedDevice::collectDiag()
{
    if (!diagLog || !diagList)
        return;
    uint32_t ids[Diag::MAX_MOVES];
    const uint8_t count = diagLog->ids(ids);
    Diag::Move move;
    for (int i = count - 1; i >= 0; --i)
    {
        if (ids[i] <= diagAddedId || !diagLog->copy(ids[i], move) || move.summary.active)
            continue;
        diagAddedId = ids[i];
        diagList->add(move.summary);
    }
}

std::string EmulatedDevice::diagDocument() const
{
    DynamicJsonDocument doc(Diag::LIVE_DOC_CAPACITY);
    if (!diagLog)
    {
        Diag::writeOff(doc.to<JsonObject>());
        return serialize(doc);
    }
    Diag::SystemLive system; // the device-only values stay unknown (null)
    system.periodMs = RoofIo::CONTROL_PERIOD_MS;
    system.watchdogLimitMs = RoofIo::WATCHDOG_LIMIT_MS;
    system.uptimeS = uptimeSeconds();
    system.simulation = "demo";
    Diag::writeLive(doc.to<JsonObject>(), roof.diagLive(), system);
    return serialize(doc);
}

std::string EmulatedDevice::diagMoves() const
{
    if (!diagLog || !diagList)
        return "{\"enabled\":false}";
    Diag::HeldIds held;
    held.count = diagLog->ids(held.ids);
    std::string all;
    std::string part;
    for (size_t i = 0; Diag::listPart(*diagList, held, cfg.diag.keepSummaries, diagClock(), i, part); ++i)
        all += part;
    return all;
}

std::string EmulatedDevice::diagMove(double id, bool csv) const
{
    Diag::Move move;
    if (!diagLog || id < 1 || !diagLog->copy(static_cast<uint32_t>(id), move))
        return "";
    std::string all;
    std::string part;
    for (size_t i = 0; csv ? Diag::csvPart(move, i, part) : Diag::movePart(move, diagClock(), i, part); ++i)
        all += part;
    return all;
}

std::string EmulatedDevice::diagClear()
{
    if (!diagLog)
        return response(200, "{\"enabled\":false}");
    const Roof::State state = roof.snapshot().status.state;
    if (Diag::moving(state))
        return response(409, DemoCore::errorJson(RoofIo::holdText(RoofIo::HoldReason::Moving)));
    diagLog->clear();
    if (diagList)
        diagList->clear();
    return response(200, "{\"success\":true}");
}
