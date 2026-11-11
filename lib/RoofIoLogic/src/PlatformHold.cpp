#include "PlatformHold.h"

#include "MotionGuards.h"

namespace SQM
{
    namespace RoofIo
    {
        namespace
        {
            // Whether the close sequence in this state holds the action. While
            // it works (park requested, or closing) nothing may cut it off or
            // drop N.I.N.A.; backing off or stood down it holds nothing, so the
            // network or the device can be put right remotely.
            bool waitsForSequence(PlatformAction action, SequenceState sequence, bool networkUp)
            {
                switch (sequence)
                {
                case SequenceState::Working:
                    if (!networkUp)
                        return action == PlatformAction::FirmwareUpload || action == PlatformAction::FilesystemUpload;
                    return action != PlatformAction::FlashWrite && action != PlatformAction::MandatoryRestart;
                default:
                    return false;
                }
            }
        } // namespace

        HoldReason platformHold(PlatformAction action, const PlatformState &state)
        {
            if (roofMoving(state.roof) || state.calibrating)
                return HoldReason::Moving;
            if (state.closeWaiting)
                return HoldReason::CloseWaiting;
            if (waitsForSequence(action, state.sequence, state.networkUp))
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
                return "The automatic close is under way: try again once the roof has closed or the weather is safe, or press Stop to "
                       "stand it down";
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
            case PlatformAction::FilesystemUpload:
                return "filesystemUpload";
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

        bool restartMayCommit(const RestartInputs &in)
        {
            if (in.faultUnsaved || in.shortWrite)
                return false;
            return in.mandatory || (!in.sequenceWorking && !in.uploadBusy);
        }

        bool networkHoldsClose(bool changing, uint32_t ageMs)
        {
            return changing && ageMs <= NETWORK_HOLD_MAX_MS;
        }

    } // namespace RoofIo
} // namespace SQM
