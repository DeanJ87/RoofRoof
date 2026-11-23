#pragma once

// The wiring check on the board (spec 023 FR-015 to FR-020): started by a
// person through the web server, run on the main loop (never in a request
// handler), its verdicts from lib/BoardLogic's WiringCheck.

#include <ArduinoJson.h>
#include <functional>
#include <string>

namespace SQM
{
    class RoofControl;

    namespace WiringRun
    {
        // setup(): the roof and what counts as busy (a move, a waiting close,
        // the close sequence working, a calibration run or a jog).
        void begin(RoofControl *roof, std::function<bool()> busy, std::function<bool()> restartNeeded);
        // Any task: "" when started, else why not.
        std::string start();
        // Main loop, every pass.
        void handle();
        // GET /api/pins/check.
        void writeDocument(JsonDocument &doc);
    } // namespace WiringRun
} // namespace SQM
