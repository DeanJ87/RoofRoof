#include "DomeBridge.h"

#include "MountMove.h"
#include "RoofText.h"

namespace SQM
{
    namespace RoofIo
    {
        namespace
        {
            Alpaca::DomeStatus at(Alpaca::ShutterState shutter, bool slewing)
            {
                Alpaca::DomeStatus out;
                out.shutter = shutter;
                out.slewing = slewing;
                return out;
            }
        } // namespace

        Alpaca::DomeStatus domeStatus(const Roof::Status &status, bool closePending, bool running, const WaitView &wait)
        {
            using Alpaca::ShutterState;
            if (!running)
                return at(ShutterState::Error, false);
            // Spec 027: at rest, the move to come once the mount is parked, or
            // the wait's failure until the next command.
            const bool moving = status.state == Roof::State::Opening || status.state == Roof::State::Closing;
            if (wait.waiting && !moving)
                return at(wait.command == Roof::Command::Open ? ShutterState::Opening : ShutterState::Closing, true);
            if (wait.failed && !moving)
                return at(ShutterState::Error, false);
            switch (status.state)
            {
            case Roof::State::Opening:
                return at(ShutterState::Opening, true);
            case Roof::State::Closing:
                return at(ShutterState::Closing, true);
            case Roof::State::Closed:
                return at(ShutterState::Closed, false);
            case Roof::State::Error:
                return at(ShutterState::Error, false);
            default:
                break;
            }
            // Open, Stopped or Unknown: a waiting close is the move to come.
            if (closePending)
                return at(ShutterState::Closing, true);
            if (status.state == Roof::State::Open)
                return at(ShutterState::Open, false);
            // Stopped part-way or unknown: neither open nor closed, so neither
            // "wait for open" nor "wait for closed" is satisfied by it.
            return at(ShutterState::Error, false);
        }

        std::vector<Alpaca::SwitchReading> switchReadings(const Roof::Status &status, bool running)
        {
            std::vector<Alpaca::SwitchReading> out;
            out.reserve(MOUNT_SWITCH_COUNT);
            for (uint8_t i = 0; i < MOUNT_SWITCH_COUNT; ++i)
            {
                const MountSwitch which = static_cast<MountSwitch>(i);
                out.push_back({mountSwitchName(which), mountSwitchDescription(which), mountSwitchValue(which, status, running)});
            }
            return out;
        }

        std::string domeWaitPrefix(const WaitView &wait)
        {
            if (wait.waiting || !wait.failed)
                return "";
            return "Last move not run: " + wait.text + ": ";
        }

        std::string domeOverridePrefix(uint8_t overrideBits)
        {
            return overrideBits != 0 ? "CHECKS OVERRIDDEN: " : "";
        }

        Roof::Command roofCommand(Alpaca::DomeAction action)
        {
            switch (action)
            {
            case Alpaca::DomeAction::OpenShutter:
                return Roof::Command::Open;
            case Alpaca::DomeAction::CloseShutter:
                return Roof::Command::Close;
            default:
                return Roof::Command::Stop; // anything else stops: the safe reading
            }
        }

        DomeDispatch domeDispatch(Alpaca::DomeAction action)
        {
            DomeDispatch out;
            out.command = roofCommand(action);
            out.origin = Roof::Origin::Client;
            return out;
        }

        bool countsAsPersonStop(Roof::Command command, Roof::Origin origin)
        {
            const bool stop = command == Roof::Command::Stop || command == Roof::Command::Abort;
            return stop && origin == Roof::Origin::Person;
        }

        std::string domeRefusalText(const std::string &reason, const std::string &text, const FaultRecord &fault)
        {
            if (reason != "faultLatched")
                return text;
            const std::string what = describeFault(fault);
            return what.empty() ? text : text + ": " + what;
        }

        Alpaca::DomeAnswer domeAnswer(
            Roof::Command command, bool accepted, const std::string &reason, const std::string &text, const FaultRecord &fault)
        {
            Alpaca::DomeAnswer answer;
            const bool stop = command == Roof::Command::Stop || command == Roof::Command::Abort;
            // Spec 023: with invalid pins ("pinSetup") nothing is driven either.
            if (accepted || (stop && (reason == "notStarted" || reason == "pinSetup")))
                return answer;
            answer.accepted = false;
            answer.message = domeRefusalText(reason, text, fault);
            return answer;
        }
    } // namespace RoofIo
} // namespace SQM
