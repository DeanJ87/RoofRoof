#include "MountMove.h"

#include <cmath>

namespace SQM
{
    namespace RoofIo
    {
        RoofEnd roofAtEnd(Roof::State state)
        {
            switch (state)
            {
            case Roof::State::Closed:
                return RoofEnd::Closed;
            case Roof::State::Open:
                return RoofEnd::Open;
            default:
                break;
            }
            return RoofEnd::None;
        }

        RoofEnd roofAtEnd(const Roof::Status &status)
        {
            // A jog or a calibration leg never counts, whatever the state reads.
            if (status.jogging || status.calibrating || status.driving)
                return RoofEnd::None;
            return roofAtEnd(status.state);
        }

        bool mountMayMove(Roof::State state)
        {
            return roofAtEnd(state) != RoofEnd::None;
        }

        bool mountMayMove(const Roof::Status &status)
        {
            return roofAtEnd(status) != RoofEnd::None;
        }

        std::string roofPlaceText(const Roof::Status &status, uint32_t travelMm)
        {
            switch (status.state)
            {
            case Roof::State::Opening:
            case Roof::State::Closing:
                return "moving";
            case Roof::State::Error:
                return "faulted, position not confirmed";
            case Roof::State::Unknown:
                return "position unknown";
            default:
                break;
            }
            if (status.jogging || status.calibrating || status.driving)
                return "moving";
            if (roofAtEnd(status) == RoofEnd::Closed)
                return "closed";
            if (roofAtEnd(status) == RoofEnd::Open)
                return "open";
            if (!status.positionKnown || travelMm == 0)
                return "part-open, position unknown";
            float percent = status.positionMm * 100.0f / static_cast<float>(travelMm);
            if (!std::isfinite(percent))
                return "part-open, position unknown";
            // Never 0 % or 100 % part-way: those read as an end.
            long whole = std::lround(percent);
            whole = whole < 1 ? 1 : whole > 99 ? 99 : whole;
            return "part-open (" + std::to_string(whole) + " %)";
        }

        std::string mountBlockedText(const Roof::Status &status, uint32_t travelMm)
        {
            const std::string place = roofPlaceText(status, travelMm);
            return "Roof " + place +
                   " with the mount not parked: moving the roof or the telescope would hit the beams. A person must check.";
        }

        bool mountSwitchValue(MountSwitch which, const Roof::Status &status, bool running)
        {
            if (!running)
                return false;
            const RoofEnd end = roofAtEnd(status);
            switch (which)
            {
            case MountSwitch::MountMayMove:
                return end != RoofEnd::None;
            case MountSwitch::RoofOpen:
                return end == RoofEnd::Open;
            case MountSwitch::RoofClosed:
                return end == RoofEnd::Closed;
            }
            return false;
        }

        const char *mountSwitchName(MountSwitch which)
        {
            switch (which)
            {
            case MountSwitch::MountMayMove:
                return "Mount may move";
            case MountSwitch::RoofOpen:
                return "Roof fully open";
            case MountSwitch::RoofClosed:
                return "Roof fully closed";
            }
            return "";
        }

        const char *mountSwitchDescription(MountSwitch which)
        {
            switch (which)
            {
            case MountSwitch::MountMayMove:
                return "True only with the roof at rest at its closed or open magnet. False part-open, moving, position "
                       "unknown or faulted: a slew, park or unpark would hit the roof beams. Read-only.";
            case MountSwitch::RoofOpen:
                return "True only with the roof at rest at its open magnet. Read-only.";
            case MountSwitch::RoofClosed:
                return "True only with the roof at rest at its closed magnet. Read-only.";
            }
            return "";
        }
    } // namespace RoofIo
} // namespace SQM
