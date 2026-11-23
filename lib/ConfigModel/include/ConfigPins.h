#pragma once

// The settings' `pins` group as JSON (spec 023), shared by the settings and
// the pin routes (GET /api/pins, POST /api/pins/validate).

#include <ArduinoJson.h>
#include <string>

#include "PinMap.h"

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
    } // namespace ConfigPins
} // namespace SQM
