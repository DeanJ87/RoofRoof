#include "CommandCheck.h"

#include "RoofText.h"

namespace SQM
{
    namespace RoofIo
    {
        CommandCheck checkCommand(
            Roof::Command command, Roof::Origin origin, const GateInputs &gates, const Roof::Controller &controller, bool closePending)
        {
            CommandCheck out;
            if (command != Roof::Command::Open && command != Roof::Command::Close)
                return out;
            const GateRefusal gate = gateCommand(command, gates);
            if (gate != GateRefusal::None)
                return CommandCheck{false, gateRefusalName(gate), describeGate(gate, gates)};
            const bool closing = controller.status().state == Roof::State::Closing || closePending;
            if (origin == Roof::Origin::Client && command == Roof::Command::Open && closing)
                return CommandCheck{false, "closing", "The roof is closing: an imaging app can't reverse a close"};
            const Roof::CommandResult result = controller.check(command, origin);
            if (!result.accepted)
                return CommandCheck{false, Roof::refusalName(result.reason), describeRefusal(result)};
            return out;
        }
    } // namespace RoofIo
} // namespace SQM
