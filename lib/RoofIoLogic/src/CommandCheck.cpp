#include "CommandCheck.h"

#include "RoofText.h"

namespace SQM
{
    namespace RoofIo
    {
        namespace
        {
            bool isStop(Roof::Command command)
            {
                return command == Roof::Command::Stop || command == Roof::Command::Abort;
            }

            // Who a refusal says can't do it.
            const char *remoteName(Roof::Origin origin)
            {
                return origin == Roof::Origin::Mqtt ? "an MQTT command" : "an imaging app";
            }

            // The roof is closing, about to, or was stopped part-way while closing.
            bool closeUnderWay(const Roof::Status &status, const CommandContext &context)
            {
                return status.state == Roof::State::Closing || context.closePending ||
                       (status.state == Roof::State::Stopped && status.target == Roof::End::Closed);
            }
        } // namespace

        bool remoteOrigin(Roof::Origin origin)
        {
            return origin == Roof::Origin::Client || origin == Roof::Origin::Mqtt;
        }

        CommandCheck checkCommand(
            Roof::Command command,
            Roof::Origin origin,
            const GateInputs &gates,
            const Roof::Controller &controller,
            const CommandContext &context)
        {
            const Roof::Status &status = controller.status();
            const bool client = remoteOrigin(origin);
            if (isStop(command))
            {
                const bool closing = status.state == Roof::State::Closing || context.closePending;
                if (client && closing && gates.weather != Safety::Verdict::Safe)
                    return CommandCheck{
                        false,
                        "unsafeClose",
                        "The roof is closing and the weather isn't safe (" +
                            (gates.weatherText.empty() ? std::string("no verdict yet") : gates.weatherText) + "): " +
                            remoteName(origin) + " can't stop it (Stop in the web UI can)"};
                return CommandCheck{};
            }
            if (command != Roof::Command::Open && command != Roof::Command::Close)
                return CommandCheck{};
            const GateRefusal gate = gateCommand(command, gates);
            if (gate != GateRefusal::None)
                return CommandCheck{false, gateRefusalName(gate), describeGate(gate, gates)};
            if (client && command == Roof::Command::Open && closeUnderWay(status, context))
                return CommandCheck{
                    false,
                    "closing",
                    std::string("The roof is closing or was stopped while closing: ") + remoteName(origin) + " can't reverse a close"};
            const Roof::CommandResult result = controller.check(command, origin);
            if (!result.accepted)
                return CommandCheck{false, Roof::refusalName(result.reason), describeRefusal(result)};
            return CommandCheck{};
        }
    } // namespace RoofIo
} // namespace SQM
