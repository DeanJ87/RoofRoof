#include "DomeBridge.h"

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

        Alpaca::DomeStatus domeStatus(const Roof::Status &status, bool closePending, bool running)
        {
            using Alpaca::ShutterState;
            if (!running)
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
            if (accepted || command == Roof::Command::Stop || command == Roof::Command::Abort)
                return answer;
            answer.accepted = false;
            answer.message = domeRefusalText(reason, text, fault);
            return answer;
        }
    } // namespace RoofIo
} // namespace SQM
