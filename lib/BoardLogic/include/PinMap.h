#pragma once

// The roof's pins as a setting (spec 023): the board, a preset and one GPIO
// per roof function, and the facts about a board and its chip that the pin
// rules (PinRules.h) check against. Pure: the firmware applies what the rules
// allow at boot (src/PinSetup.cpp). The board profile idea is SQMeter's
// (lib/ConfigModel/include/BoardProfile.h), as data rather than a build flag.

#include <cstddef>
#include <cstdint>
#include <string>

namespace SQM
{
    namespace Board
    {
        enum class BoardId : uint8_t
        {
            Esp32Devkit = 0, // ESP32 DevKit (WROOM or WROVER), Wi-Fi
            Wt32Eth01 = 1,   // WT32-ETH01: ESP32 with a LAN8720 RMII PHY
        };

        enum class Preset : uint8_t
        {
            Devkit = 0,        // ESP32 DevKit: the map before spec 023, without the rain contact
            Wt32CarrierV1 = 1, // WT32-ETH01 on the RoofRoof carrier v1
            Custom = 2,        // each pin set by hand, on either board
            Unset = 3,         // a new device: no board chosen yet, nothing driven
        };

        // The roof's functions, in the order the rules report them.
        enum class Function : uint8_t
        {
            Rpwm,
            Lpwm,
            Enable, // R_EN and L_EN joined: one EN line by design
            CurrentR,
            CurrentL,
            Supply,
            Hall,
            ParkA,
            ParkB,
            None = 255, // an issue about the map as a whole (the board)
        };
        constexpr size_t FUNCTION_COUNT = 9;

        struct PinMap
        {
            Preset preset = Preset::Devkit;
            BoardId board = BoardId::Esp32Devkit;
            uint8_t rpwm = 25;
            uint8_t lpwm = 26;
            uint8_t enable = 27;
            uint8_t currentR = 34;
            uint8_t currentL = 35;
            uint8_t supply = 36;
            uint8_t hall = 39;
            uint8_t parkA = 32;
            uint8_t parkB = 33;
            // Each park input's pull-up: the chip's own, or one fitted on the board
            // (then the input may be on 34-39, which have none). Not part of a preset.
            bool parkAExternalPullup = false;
            bool parkBExternalPullup = false;

            uint8_t pin(Function function) const;
            void setPin(Function function, uint8_t gpio);
            // Same board and pins (the preset label aside).
            bool samePins(const PinMap &other) const;
            bool operator==(const PinMap &other) const
            {
                return preset == other.preset && samePins(other) && parkAExternalPullup == other.parkAExternalPullup &&
                       parkBExternalPullup == other.parkBExternalPullup;
            }
            bool operator!=(const PinMap &other) const { return !(*this == other); }
        };

        // A named preset's map; Custom and Unset give the DevKit's pins with that label.
        PinMap presetMap(Preset preset);
        constexpr bool isNamed(Preset preset)
        {
            return preset == Preset::Devkit || preset == Preset::Wt32CarrierV1;
        }

        // The settings' and API's names ("devkit", "esp32-devkit", "rpwm").
        const char *presetName(Preset preset);
        bool parsePreset(const std::string &name, Preset &out);
        const char *boardName(BoardId board);
        bool parseBoard(const std::string &name, BoardId &out);
        const char *functionName(Function function);
        // How a person names it ("RPWM", "EN", "Hall sensor").
        const char *functionLabel(Function function);
        const char *presetLabel(Preset preset);
        const char *boardLabel(BoardId board);

        constexpr uint64_t gpioBit(unsigned pin)
        {
            return pin < 64 ? (1ULL << pin) : 0;
        }
        constexpr uint64_t gpioRange(unsigned first, unsigned last)
        {
            return first > last ? 0 : gpioBit(first) | gpioRange(first + 1, last);
        }

        // What the rules need to know about a board and its chip.
        struct Facts
        {
            BoardId board = BoardId::Esp32Devkit;
            uint64_t exists = 0;      // GPIOs the chip has, flash pins included
            uint64_t flash = 0;       // the SPI flash
            uint64_t psram = 0;       // PSRAM on WROVER modules (reserved on every board)
            bool requireWt32 = false; // the device has seen a LAN8720 here: only the WT32-ETH01 board
            uint64_t uart = 0;        // UART0: the log and USB flashing
            uint64_t ethernet = 0;    // the board's Ethernet
            uint64_t header = 0;      // the pins the board brings out (0: all that exist)
            uint64_t inputOnly = 0;   // no output driver, no pull-up
            uint64_t adc1 = 0;        // readable while Wi-Fi runs
            uint64_t straps = 0;      // sampled at reset
            uint64_t bootPulses = 0;  // driven by the chip during boot
            uint64_t probe = 0;       // driven by the WT32-ETH01 probe at boot

            static bool has(uint64_t set, unsigned pin) { return pin < 64 && (set & gpioBit(pin)) != 0; }
        };

        // The classic ESP32's facts on `board`. `psram` (the chip reports PSRAM)
        // no longer changes them: 16 and 17 are reserved everywhere.
        Facts facts(BoardId board, bool psram);

        // The WT32-ETH01's Ethernet (LAN8720, research R8).
        constexpr uint8_t ETH_PHY_ADDRESS = 1;
        constexpr uint8_t ETH_MDC = 23;
        constexpr uint8_t ETH_MDIO = 18;
        constexpr uint8_t ETH_POWER = 16; // the 50 MHz oscillator's enable
        constexpr uint8_t ETH_CLOCK = 0;  // the RMII reference clock, in
    } // namespace Board
} // namespace SQM
