#include "PinMap.h"

namespace SQM
{
    namespace Board
    {
        namespace
        {
            constexpr const char *PRESET_NAMES[] = {"devkit", "wt32-carrier-v1", "custom", "unset"};
            constexpr size_t PRESET_COUNT = 4;
            constexpr const char *BOARD_NAMES[] = {"esp32-devkit", "wt32-eth01"};
            constexpr const char *FUNCTION_NAMES[] = {"rpwm", "lpwm", "enable", "currentR", "currentL", "supply", "hall", "parkA", "parkB"};
            constexpr const char *FUNCTION_LABELS[] = {
                "RPWM", "LPWM", "EN", "R_IS", "L_IS", "Supply sense", "Hall sensor", "Park switch A", "Park switch B"};

            uint8_t *slot(PinMap &map, Function function)
            {
                switch (function)
                {
                case Function::Rpwm:
                    return &map.rpwm;
                case Function::Lpwm:
                    return &map.lpwm;
                case Function::Enable:
                    return &map.enable;
                case Function::CurrentR:
                    return &map.currentR;
                case Function::CurrentL:
                    return &map.currentL;
                case Function::Supply:
                    return &map.supply;
                case Function::Hall:
                    return &map.hall;
                case Function::ParkA:
                    return &map.parkA;
                case Function::ParkB:
                    return &map.parkB;
                default:
                    return nullptr;
                }
            }

            // Classic ESP32 (datasheet, research R2).
            constexpr uint64_t ESP32_EXISTS = gpioRange(0, 19) | gpioRange(21, 23) | gpioRange(25, 27) | gpioRange(32, 39);
            constexpr uint64_t ESP32_FLASH = gpioRange(6, 11);
            constexpr uint64_t ESP32_PSRAM = gpioBit(16) | gpioBit(17);
            constexpr uint64_t ESP32_UART0 = gpioBit(1) | gpioBit(3);
            constexpr uint64_t ESP32_INPUT_ONLY = gpioRange(34, 39);
            constexpr uint64_t ESP32_ADC1 = gpioRange(32, 39);
            constexpr uint64_t ESP32_STRAPS = gpioBit(0) | gpioBit(2) | gpioBit(5) | gpioBit(12) | gpioBit(15);
            constexpr uint64_t ESP32_BOOT_PULSES = gpioBit(5) | gpioBit(14) | gpioBit(15);
            // WT32-ETH01 (research R2): the RMII, MDIO/MDC, clock and oscillator pins, and its header.
            constexpr uint64_t WT32_ETHERNET = gpioBit(0) | gpioBit(16) | gpioBit(18) | gpioBit(19) | gpioRange(21, 23) | gpioRange(25, 27);
            constexpr uint64_t WT32_HEADER = gpioRange(0, 5) | gpioBit(12) | gpioBit(14) | gpioBit(15) | gpioBit(17) | gpioBit(32) |
                                             gpioBit(33) | gpioBit(35) | gpioBit(36) | gpioBit(39);
        } // namespace

        uint8_t PinMap::pin(Function function) const
        {
            uint8_t *where = slot(const_cast<PinMap &>(*this), function);
            return where != nullptr ? *where : 255;
        }

        void PinMap::setPin(Function function, uint8_t gpio)
        {
            uint8_t *where = slot(*this, function);
            if (where != nullptr)
                *where = gpio;
        }

        bool PinMap::samePins(const PinMap &other) const
        {
            if (board != other.board)
                return false;
            for (size_t i = 0; i < FUNCTION_COUNT; ++i)
                if (pin(static_cast<Function>(i)) != other.pin(static_cast<Function>(i)))
                    return false;
            return true;
        }

        PinMap presetMap(Preset preset)
        {
            PinMap map; // the DevKit's
            map.preset = preset;
            if (preset != Preset::Wt32CarrierV1)
                return map;
            map.board = BoardId::Wt32Eth01;
            map.rpwm = 14;
            map.lpwm = 15;
            map.enable = 4; // 10 kOhm pull-down on the carrier
            map.currentR = 35;
            map.currentL = 32;
            map.supply = 36;
            map.hall = 39;
            map.parkA = 33;
            map.parkB = 5;
            return map;
        }

        const char *presetName(Preset preset)
        {
            const auto index = static_cast<size_t>(preset);
            return index < PRESET_COUNT ? PRESET_NAMES[index] : "unset";
        }

        bool parsePreset(const std::string &name, Preset &out)
        {
            for (size_t i = 0; i < PRESET_COUNT; ++i)
                if (name == PRESET_NAMES[i])
                {
                    out = static_cast<Preset>(i);
                    return true;
                }
            return false;
        }

        const char *boardName(BoardId board)
        {
            return board == BoardId::Wt32Eth01 ? BOARD_NAMES[1] : BOARD_NAMES[0];
        }

        bool parseBoard(const std::string &name, BoardId &out)
        {
            for (size_t i = 0; i < 2; ++i)
                if (name == BOARD_NAMES[i])
                {
                    out = static_cast<BoardId>(i);
                    return true;
                }
            return false;
        }

        const char *functionName(Function function)
        {
            const auto index = static_cast<size_t>(function);
            return index < FUNCTION_COUNT ? FUNCTION_NAMES[index] : "";
        }

        const char *functionLabel(Function function)
        {
            const auto index = static_cast<size_t>(function);
            return index < FUNCTION_COUNT ? FUNCTION_LABELS[index] : "";
        }

        const char *presetLabel(Preset preset)
        {
            switch (preset)
            {
            case Preset::Devkit:
                return "ESP32 DevKit";
            case Preset::Wt32CarrierV1:
                return "WT32-ETH01 + RoofRoof carrier v1";
            case Preset::Unset:
                return "No board chosen";
            default:
                return "Custom";
            }
        }

        const char *boardLabel(BoardId board)
        {
            return board == BoardId::Wt32Eth01 ? "WT32-ETH01" : "ESP32 DevKit";
        }

        Facts facts(BoardId board, bool psram)
        {
            Facts f;
            f.board = board;
            f.exists = ESP32_EXISTS;
            f.flash = ESP32_FLASH;
            f.psram = psram ? ESP32_PSRAM : 0;
            f.uart = ESP32_UART0;
            f.inputOnly = ESP32_INPUT_ONLY;
            f.adc1 = ESP32_ADC1;
            f.straps = ESP32_STRAPS;
            f.bootPulses = ESP32_BOOT_PULSES;
            if (board == BoardId::Wt32Eth01)
            {
                f.ethernet = WT32_ETHERNET;
                f.header = WT32_HEADER;
            }
            return f;
        }
    } // namespace Board
} // namespace SQM
