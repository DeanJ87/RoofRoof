#include "PlatformHold.h"

#include "MotionGuards.h"

namespace SQM
{
    namespace RoofIo
    {
        namespace
        {
            // The actions that also wait for the close sequence.
            bool waitsForSequence(PlatformAction action)
            {
                return action == PlatformAction::Restart || action == PlatformAction::FirmwareUpload ||
                       action == PlatformAction::WifiChange;
            }
        } // namespace

        HoldReason platformHold(PlatformAction action, const PlatformState &state)
        {
            if (roofMoving(state.roof))
                return HoldReason::Moving;
            if (state.closeWaiting)
                return HoldReason::CloseWaiting;
            if (state.sequenceBusy && waitsForSequence(action))
                return HoldReason::CloseSequence;
            return HoldReason::None;
        }

        const char *holdText(HoldReason reason)
        {
            switch (reason)
            {
            case HoldReason::Moving:
                return "The roof is moving: stop it first, then try again";
            case HoldReason::CloseWaiting:
                return "A close is waiting to start: try again once the roof has closed";
            case HoldReason::CloseSequence:
                return "The automatic close is under way: try again once the roof has closed or the weather is safe";
            default:
                return "";
            }
        }

        const char *holdName(HoldReason reason)
        {
            switch (reason)
            {
            case HoldReason::Moving:
                return "moving";
            case HoldReason::CloseWaiting:
                return "closeWaiting";
            case HoldReason::CloseSequence:
                return "closeSequence";
            default:
                return "none";
            }
        }

        const char *platformActionName(PlatformAction action)
        {
            switch (action)
            {
            case PlatformAction::FlashWrite:
                return "flashWrite";
            case PlatformAction::Restart:
                return "restart";
            case PlatformAction::MandatoryRestart:
                return "mandatoryRestart";
            case PlatformAction::FirmwareUpload:
                return "firmwareUpload";
            case PlatformAction::WifiChange:
                return "wifiChange";
            default:
                return "wifiScan";
            }
        }

        bool hotspotRefuses(Roof::Command command, bool viaHotspot)
        {
            return viaHotspot && command == Roof::Command::Open;
        }
    } // namespace RoofIo
} // namespace SQM
