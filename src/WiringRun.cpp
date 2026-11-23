#include "WiringRun.h"

#include <Arduino.h>
#include <atomic>
#include <mutex>

#include "ConfigPins.h"
#include "EthernetLink.h"
#include "EthernetProbe.h"
#include "Logger.h"
#include "PinSetup.h"
#include "RoofControl.h"
#include "RoofText.h"
#include "WiringCheck.h"

namespace SQM
{
    namespace WiringRun
    {
        namespace
        {
            constexpr const char *TAG = "Wiring";

            enum class State : uint8_t
            {
                Idle,
                Running,
                Done,
            };

            RoofControl *theRoof = nullptr;
            std::function<bool()> roofBusy;
            std::function<bool()> needsRestart;
            std::atomic<uint8_t> state{static_cast<uint8_t>(State::Idle)};
            std::mutex resultLock;
            Board::WiringResult result; // guarded by resultLock
            uint32_t finishedAtMs = 0;  // guarded by resultLock

            Board::EthernetFacts ethernetFacts()
            {
                Board::EthernetFacts e;
                if (EthernetLink::present())
                {
                    e.mode = Board::EthernetFacts::Mode::Running;
                    e.started = EthernetLink::started();
                    e.phyFound = e.started;
                    e.link = EthernetLink::linkUp();
                    e.hasAddress = EthernetLink::hasAddress();
                    e.speedMbps = EthernetLink::speedMbps();
                    return e;
                }
                if (PinSetup::board() == Board::BoardId::Wt32Eth01)
                {
                    e.mode = Board::EthernetFacts::Mode::SkippedWifiOnly; // the board's own PHY, deliberately off
                    return e;
                }
                if (PinSetup::psram())
                    return e; // SkippedPsram
                const Board::PinMap &pins = PinSetup::running();
                for (uint8_t pin : {Board::ETH_CLOCK, Board::ETH_POWER, Board::ETH_MDIO, Board::ETH_MDC})
                    for (size_t i = 0; i < Board::FUNCTION_COUNT; ++i)
                        if (pins.pin(static_cast<Board::Function>(i)) == pin)
                        {
                            e.mode = Board::EthernetFacts::Mode::SkippedPin;
                            e.usedPin = pin;
                            return e;
                        }
                EthernetProbe::run(e);
                return e;
            }

            void run()
            {
                Board::WiringInputs in;
                std::string why;
                const MotorDriver::EnableProbe en = theRoof->probeEnable(why);
                in.en = en == MotorDriver::EnableProbe::Low    ? Board::EnProbe::Low
                        : en == MotorDriver::EnableProbe::High ? Board::EnProbe::High
                        : why.empty()                          ? Board::EnProbe::NotRun
                                                               : Board::EnProbe::Busy;
                theRoof->wiringReadings(in);
                if (in.en == Board::EnProbe::Busy)
                    in.ethernet.mode = Board::EthernetFacts::Mode::SkippedBusy; // nothing more is driven while busy
                else
                    in.ethernet = ethernetFacts();
                Board::WiringResult out = Board::evaluate(in);
                {
                    const std::lock_guard<std::mutex> held(resultLock);
                    result = std::move(out);
                    finishedAtMs = millis();
                }
                state = static_cast<uint8_t>(State::Done);
                Logger::info(TAG, "Wiring check done");
            }
        } // namespace

        void begin(RoofControl *roof, std::function<bool()> busy, std::function<bool()> restartNeeded)
        {
            theRoof = roof;
            roofBusy = std::move(busy);
            needsRestart = std::move(restartNeeded);
        }

        std::string start()
        {
            if (theRoof == nullptr || !theRoof->running())
                return "The wiring check isn't available: the roof controller didn't start";
            if (!PinSetup::valid())
                return RoofIo::pinSetupText(PinSetup::boot().reason, PinSetup::boot().unset);
            if (needsRestart && needsRestart())
                return "Restart first: the saved pins aren't in effect yet";
            if ((roofBusy && roofBusy()) || theRoof->moving() || theRoof->calibrating())
                return "The wiring check can't run while the roof is busy";
            uint8_t expected = static_cast<uint8_t>(State::Idle);
            if (!state.compare_exchange_strong(expected, static_cast<uint8_t>(State::Running)))
            {
                expected = static_cast<uint8_t>(State::Done);
                if (!state.compare_exchange_strong(expected, static_cast<uint8_t>(State::Running)))
                    return "A wiring check is already running";
            }
            Logger::info(TAG, "Wiring check started");
            return "";
        }

        void handle()
        {
            if (state.load() == static_cast<uint8_t>(State::Running))
                run();
        }

        void writeDocument(JsonDocument &doc)
        {
            const std::lock_guard<std::mutex> held(resultLock);
            ConfigPins::writeWiring(doc.to<JsonObject>(), static_cast<ConfigPins::CheckState>(state.load()), result, finishedAtMs);
        }
    } // namespace WiringRun
} // namespace SQM
