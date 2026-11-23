#include "emulated_device.h"

#include "RoofSettings.h"
#include "RoofText.h"

// Spec 023 (contracts/pins-api.md): the pin routes in the demo, through the
// firmware's own rules and document writers. The demo board is a DevKit
// without PSRAM; its wiring check reads the simulated roof, with the 10 k EN
// pull-down fitted and no Ethernet PHY.

namespace
{
    Board::WiringInputs simulatedWiring(const RoofIo::Snapshot &s, const Config &cfg)
    {
        Board::WiringInputs in;
        in.en = Board::EnProbe::Low;
        in.hallCounts = s.status.hallCounts;
        in.hallRailLow = Roof::HALL_RAIL_LOW_COUNTS;
        in.hallRailHigh = Roof::HALL_RAIL_HIGH_COUNTS;
        in.hallMin = RoofConfig::MIN_HALL_COUNTS;
        in.hallMax = RoofConfig::MAX_HALL_COUNTS;
        in.supplyVolts = s.supplyVolts;
        in.supplyLimit = cfg.roof.minSupplyVolts;
        in.fittedSwitches = s.fittedSwitches;
        auto level = [](RoofIo::Contact c)
        {
            return c == RoofIo::Contact::Open     ? Board::SwitchLevel::Open
                   : c == RoofIo::Contact::Closed ? Board::SwitchLevel::Closed
                                                  : Board::SwitchLevel::Unreadable;
        };
        in.parkA = level(s.switch1);
        in.parkB = level(s.switch2);
        in.currentSense = cfg.roof.currentSenseEnabled;
        for (size_t side = 0; side < 2; ++side)
        {
            in.currentMillivolts[side] = 22.0f;
            in.currentAmps[side] = 0.03f;
        }
        in.ethernet.mode = Board::EthernetFacts::Mode::Probed; // a DevKit: nothing answers
        return in;
    }
} // namespace

std::string EmulatedDevice::pins(const std::string &action, const std::string &body)
{
    runRoof();
    const Board::Facts facts = Board::facts(runningPins.board, false);
    if (action == "validate")
    {
        StaticJsonDocument<512> in;
        Board::PinMap map = cfg.pins;
        std::string error = "A pin map must be a JSON object";
        if (deserializeJson(in, body) || !in.is<JsonObject>() || !ConfigPins::read(in.as<JsonObjectConst>(), map, &error))
            return response(400, DemoCore::errorJson(error));
        DynamicJsonDocument out(ConfigPins::PINS_DOCUMENT_BYTES / 2);
        ConfigPins::writeValidation(out.to<JsonObject>(), map, Board::check(map, Board::facts(map.board, false)));
        return response(200, serialize(out));
    }
    const Board::BootPins boot = Board::bootPins(runningPins, facts, false);
    if (action == "check")
    {
        if (!boot.valid)
            return response(409, DemoCore::errorJson(RoofIo::pinSetupText(boot.reason)));
        if (cfg.pins != runningPins)
            return response(409, DemoCore::errorJson("Restart first: the saved pins aren't in effect yet"));
        if (!platformHold("restart").empty())
            return response(409, DemoCore::errorJson("The roof is busy: the wiring check runs only while it is still"));
        wiring = Board::evaluate(simulatedWiring(roof.snapshot(), cfg));
        wiringState = ConfigPins::CheckState::Done;
        wiringAtMs = nowMs;
        return response(202, "{\"started\":true}");
    }
    if (action == "checkStatus")
    {
        DynamicJsonDocument out(ConfigPins::CHECK_DOCUMENT_BYTES);
        ConfigPins::writeWiring(out.to<JsonObject>(), wiringState, wiring, wiringAtMs);
        return response(200, serialize(out));
    }
    ConfigPins::PinsView view;
    view.running = runningPins;
    view.saved = cfg.pins;
    view.boot = boot;
    view.probed = runningPins.preset == Board::Preset::Unset;
    view.board = runningPins.preset == Board::Preset::Unset ? Board::BoardId::Esp32Devkit : runningPins.board;
    DynamicJsonDocument out(ConfigPins::PINS_DOCUMENT_BYTES);
    ConfigPins::writePinsDocument(out.to<JsonObject>(), view);
    return response(200, serialize(out));
}
