#include "SwitchWatchStep.h"

namespace SQM
{
    namespace RoofIo
    {
        uint8_t stepSwitchWatch(Safety::SwitchWatch &watch, const WatchStep &step)
        {
            Safety::watchReadings(watch, step.fitted, parkReading(step.switch1), parkReading(step.switch2));
            const bool reachedOpen =
                step.now == Roof::State::Open && step.before != Roof::State::Open && step.before != Roof::State::Unknown;
            if (reachedOpen && step.switchesOnly && !step.calibrating)
                Safety::countOpening(watch, step.fitted);
            return Safety::stuckSwitch(watch, step.fitted, step.switchesOnly);
        }
    } // namespace RoofIo
} // namespace SQM
