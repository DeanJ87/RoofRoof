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

            // A client's stop of a close while the weather isn't safe, or (spec
            // 013) while daylight wants the roof closed: refused, a person's isn't.
            // Spec 014: with the owner's remoteStopsClose it is accepted and
            // counts as a person's stop (the stand-down's critical alert follows).
            CommandCheck checkClientStop(
                Roof::Command command,
                Roof::Origin origin,
                const GateInputs &gates,
                const Roof::Status &status,
                const CommandContext &context)
            {
                const bool closing = status.state == Roof::State::Closing || context.closePending;
                const bool wanted = gates.weather != Safety::Verdict::Safe || gates.daylightCloses;
                // Only a Stop (it keeps the position estimate): an abort would make
                // the position unknown, and the automatic close couldn't move again.
                if (closing && wanted && context.remoteStopsClose && command == Roof::Command::Stop)
                {
                    CommandCheck honoured;
                    honoured.standsDown = true;
                    return honoured;
                }
                if (closing && gates.weather != Safety::Verdict::Safe)
                    return CommandCheck{
                        false,
                        "unsafeClose",
                        "The roof is closing and the weather isn't safe (" +
                            (gates.weatherText.empty() ? std::string("no verdict yet") : gates.weatherText) + "): " + remoteName(origin) +
                            " can't stop it (Stop in the web UI can)"};
                if (closing && gates.daylightCloses)
                    return CommandCheck{
                        false,
                        "unsafeClose",
                        "The roof is closing in daylight (" +
                            (gates.daylightText.empty() ? std::string("the sun is up") : gates.daylightText) + "): " + remoteName(origin) +
                            " can't stop it (Stop in the web UI can)"};
                return CommandCheck{};
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
                return client ? checkClientStop(command, origin, gates, status, context) : CommandCheck{};
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
