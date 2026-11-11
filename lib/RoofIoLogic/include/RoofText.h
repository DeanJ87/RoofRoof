#pragma once

// The wording of roof faults and refusals for people (constitution III: every
// fault and refusal states the measured value and the limit, or the input
// that failed; spec 003 FR-022). English, like the rest of the device's text.

#include <string>

#include "FaultRecord.h"
#include "RoofGates.h"
#include "RoofTypes.h"

namespace SQM
{
    namespace RoofIo
    {
        // "Supply 10.9 V < 11.0 V", "Control step 140 ms late > 100 ms (watchdog)"; "" for no fault.
        std::string describeFault(const FaultRecord &record);
        // A refused open or close: "Supply 10.9 V < 11.0 V", "A fault is latched: ...".
        std::string describeRefusal(const Roof::CommandResult &result);
        // The supply against its minimum: "Supply 10.9 V < 11.0 V", or
        // "Supply unreadable (minimum 11.0 V)" (spec 007's supply alert).
        std::string describeSupply(float volts, float minimum);
        // A gate refusal: "Weather not safe: unsafe: rain", "Mount not parked: ...".
        std::string describeGate(GateRefusal refusal, const GateInputs &in);
    } // namespace RoofIo
} // namespace SQM
