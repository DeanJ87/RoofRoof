#pragma once

// The settings' `pins` group as JSON (spec 023), shared by the settings and
// the pin routes (GET /api/pins, POST /api/pins/validate).

#include <ArduinoJson.h>
#include <string>

#include "Analog.h"
#include "PinMap.h"
#include "PinRules.h"
#include "WiringCheck.h"

namespace SQM
{
    namespace ConfigPins
    {
        void write(JsonObject out, const Board::PinMap &map);
        // A pins object onto `map`. A named preset sets the board and every
        // pin first; pins given with it are applied on top, so the rules
        // refuse any that differ. False with the reason for an unknown name or
        // a value that isn't a GPIO number.
        bool read(JsonObjectConst in, Board::PinMap &map, std::string *error);
        // The `analog` group (dividers and trims), the same way.
        void writeAnalog(JsonObject out, const Board::AnalogConfig &analog);
        bool readAnalog(JsonObjectConst in, Board::AnalogConfig &analog, std::string *error);

        // The documents of contracts/pins-api.md, shared by the firmware and the demo.
        struct PinsView
        {
            Board::PinMap running;
            Board::PinMap saved;
            Board::BootPins boot;
            bool psram = false;
            bool probed = false; // the boot probe ran (no board chosen)
            Board::BoardId detected = Board::BoardId::Esp32Devkit;
            Board::BoardId board = Board::BoardId::Esp32Devkit; // in effect
            Board::AnalogConfig analog; // saved (applied by the roof once no move is under way)
        };
        constexpr size_t PINS_DOCUMENT_BYTES = 6144;
        void writePinsDocument(JsonObject out, const PinsView &view);
        // `analog`: the dividers and trims to check too (nullptr: none sent).
        void writeValidation(JsonObject out, const Board::PinMap &map, const Board::PinReport &report, const Board::AnalogConfig *analog = nullptr);

        enum class CheckState : uint8_t
        {
            Idle,
            Running,
            Done,
        };
        constexpr size_t CHECK_DOCUMENT_BYTES = 3072;
        void writeWiring(JsonObject out, CheckState state, const Board::WiringResult &result, uint32_t finishedAtMs);
    } // namespace ConfigPins
} // namespace SQM
