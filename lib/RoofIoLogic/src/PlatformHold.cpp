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
            // drop N.I.N.A.; a stand-down by a person only holds a new firmware
            // (it might not read the stand-down back); a back-off or another
            // stand-down holds nothing, so the network or the device can be
            // put right remotely.
            bool waitsForSequence(PlatformAction action, SequenceState sequence)
            {
                switch (sequence)
                {
                case SequenceState::Working:
                    return action != PlatformAction::FlashWrite && action != PlatformAction::MandatoryRestart;
                case SequenceState::HeldByPerson:
                    return action == PlatformAction::FirmwareUpload;
                default:
                    return false;
                }
            }
        } // namespace

        HoldReason platformHold(PlatformAction action, const PlatformState &state)
        {
            if (roofMoving(state.roof))
                return HoldReason::Moving;
            if (state.closeWaiting)
                return HoldReason::CloseWaiting;
            if (waitsForSequence(action, state.sequence))
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

        bool hotspotRefuses(Roof::Command command, bool hotspotOn)
        {
            return hotspotOn && command == Roof::Command::Open;
        }
    } // namespace RoofIo
} // namespace SQM
