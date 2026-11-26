#include "StaleRide.h"

namespace SQM
{
    namespace RoofIo
    {
        namespace
        {
            bool moving(Roof::State state)
            {
                return state == Roof::State::Opening || state == Roof::State::Closing;
            }

            bool reached(Roof::State state, Roof::End target)
            {
                return (state == Roof::State::Closed && target == Roof::End::Closed) ||
                       (state == Roof::State::Open && target == Roof::End::Open);
            }

            const char *moveWord(Roof::End target)
            {
                return target == Roof::End::Open ? "open" : "close";
            }
        } // namespace

        StaleStep StaleRide::step(const Roof::Status &status, bool onStale)
        {
            if (moving(status.state))
            {
                end = status.target;
                if (!onStale || rode)
                    return StaleStep::None;
                rode = true;
                return StaleStep::Began;
            }
            if (!rode)
                return StaleStep::None;
            rode = false;
            return reached(status.state, end) ? StaleStep::Finished : StaleStep::None;
        }

        std::string staleRideText(StaleStep step, Roof::End target)
        {
            const std::string move = moveWord(target);
            switch (step)
            {
            case StaleStep::Began:
            {
                std::string text =
                    move + " continues without a fresh park reading (N.I.N.A. unreachable): it started with the park confirmed";
                text[0] = static_cast<char>(text[0] - 'a' + 'A');
                return text;
            }
            case StaleStep::Finished:
                return "Finished the " + move + " without a fresh park reading (N.I.N.A. unreachable)";
            default:
                return "";
            }
        }
    } // namespace RoofIo
} // namespace SQM
