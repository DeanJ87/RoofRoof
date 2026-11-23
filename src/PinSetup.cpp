#include "PinSetup.h"

#include <Arduino.h>
#include <driver/gpio.h>

#include "EthernetProbe.h"
#include "Logger.h"
#include "MotorDriver.h"

namespace SQM
{
    namespace PinSetup
    {
        namespace
        {
            constexpr const char *TAG = "Pins";
            Board::PinMap inEffect;
            Board::BootPins decided;
            bool chipPsram = false;
            bool chosen = false;
            bool probed = false;
            Board::BoardId detected = Board::BoardId::Esp32Devkit;

            // FR-034: nothing driven yet, so the probe meets every pin as reset left it.
            void detectBoard()
            {
                if (chipPsram)
                {
                    Logger::warn(TAG, "No board chosen; not probing for a WT32-ETH01 (PSRAM uses GPIO16)");
                    return;
                }
                Board::EthernetFacts facts;
                EthernetProbe::run(facts);
                probed = true;
                detected = EthernetProbe::foundWt32(facts) ? Board::BoardId::Wt32Eth01 : Board::BoardId::Esp32Devkit;
                Logger::info(
                    TAG,
                    "No board chosen; the probe found %s",
                    detected == Board::BoardId::Wt32Eth01 ? "a WT32-ETH01 (LAN8720)" : "no Ethernet PHY");
            }

            // An input with no pull: a pin's state after reset (research R5).
            void holdAsInput(uint8_t pin)
            {
                gpio_config_t io = {};
                io.pin_bit_mask = 1ULL << pin;
                io.mode = GPIO_MODE_INPUT;
                io.pull_up_en = GPIO_PULLUP_DISABLE;
                io.pull_down_en = GPIO_PULLDOWN_DISABLE;
                io.intr_type = GPIO_INTR_DISABLE;
                gpio_config(&io);
            }
        } // namespace

        void begin(const Config &config, bool storedRejected)
        {
            chipPsram = psramFound();
            inEffect = config.pins;
            chosen = !storedRejected && inEffect.preset != Board::Preset::Unset;
            if (!chosen)
                detectBoard();
            decided = Board::bootPins(inEffect, facts(), storedRejected);
            if (decided.valid)
            {
                MotorDriver::configure(inEffect.rpwm, inEffect.lpwm, inEffect.enable);
                MotorDriver::holdOff();
                Logger::info(
                    TAG,
                    "%s on the %s: RPWM %u, LPWM %u, EN %u, R_IS %u, L_IS %u, supply %u, Hall %u, park %u/%u",
                    Board::presetLabel(inEffect.preset),
                    Board::boardLabel(inEffect.board),
                    inEffect.rpwm,
                    inEffect.lpwm,
                    inEffect.enable,
                    inEffect.currentR,
                    inEffect.currentL,
                    inEffect.supply,
                    inEffect.hall,
                    inEffect.parkA,
                    inEffect.parkB);
                return;
            }
            // Nothing driven: the EN pull-down holds the driver off.
            MotorDriver::disable();
            for (uint8_t pin : decided.holdAsInputs)
                holdAsInput(pin);
            Logger::error(TAG, "Pin setup invalid, nothing is driven: %s", decided.reason.c_str());
        }

        bool valid()
        {
            return decided.valid;
        }

        const Board::PinMap &running()
        {
            return inEffect;
        }

        const Board::BootPins &boot()
        {
            return decided;
        }

        bool psram()
        {
            return chipPsram;
        }

        Board::Facts facts()
        {
            return Board::facts(board(), chipPsram);
        }

        bool boardProbed()
        {
            return probed;
        }

        Board::BoardId detectedBoard()
        {
            return detected;
        }

        Board::BoardId board()
        {
            return chosen ? inEffect.board : detected;
        }

        bool ethernetBoard()
        {
            return board() == Board::BoardId::Wt32Eth01;
        }
    } // namespace PinSetup
} // namespace SQM
