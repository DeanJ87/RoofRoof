#include "PlatformGuard.h"

#include <atomic>

#include "FlashGuard.h"
#include "RoofControl.h"

namespace SQM
{
    namespace PlatformGuard
    {
        namespace
        {
            // Set once in setup() before any other task can ask.
            RoofControl *theRoof = nullptr;
            std::function<bool()> sequenceProbe;

            bool sequenceBusy()
            {
                return sequenceProbe && sequenceProbe();
            }

            RoofIo::PlatformState latest()
            {
                RoofIo::PlatformState state;
                if (theRoof != nullptr)
                {
                    state.roof = theRoof->state();
                    state.closeWaiting = theRoof->closeWaiting();
                }
                else
                {
                    state.roof = Roof::State::Closed; // no roof: nothing moves
                }
                state.sequenceBusy = sequenceBusy();
                return state;
            }

            // The close sequence's part of the rule (the roof's is checked under its lock).
            RoofIo::HoldReason sequenceHold(RoofIo::PlatformAction action)
            {
                RoofIo::PlatformState state;
                state.roof = Roof::State::Closed;
                state.sequenceBusy = sequenceBusy();
                return RoofIo::platformHold(action, state);
            }

            RoofIo::HoldReason underLock(const std::function<void()> &action, bool wait = true)
            {
                if (theRoof == nullptr)
                {
                    action();
                    return RoofIo::HoldReason::None;
                }
                return theRoof->whileStill(action, wait);
            }
        } // namespace

        void begin(RoofControl *roof, std::function<bool()> busy)
        {
            theRoof = roof;
            sequenceProbe = std::move(busy);
        }

        RoofIo::HoldReason hold(RoofIo::PlatformAction action)
        {
            return RoofIo::platformHold(action, latest());
        }

        FlashWrite::FlashWrite(RoofIo::PlatformAction action)
        {
            reason = sequenceHold(action);
            if (reason != RoofIo::HoldReason::None)
                return;
            reason = underLock(
                [this]()
                {
                    FlashGuard::holdShort();
                    held = true;
                });
        }

        FlashWrite::~FlashWrite()
        {
            if (held)
                FlashGuard::releaseShort();
        }

        uint32_t beginUpload(RoofIo::PlatformAction action, RoofIo::HoldReason &why, bool closedOnly)
        {
            why = sequenceHold(action);
            if (why != RoofIo::HoldReason::None)
                return 0;
            uint32_t token = 0;
            bool notClosed = false;
            why = underLock(
                [&]()
                {
                    // Under the lock the published state matches the controller's.
                    if (closedOnly && theRoof != nullptr && theRoof->state() != Roof::State::Closed)
                    {
                        notClosed = true;
                        return;
                    }
                    token = FlashGuard::begin();
                });
            if (notClosed)
                why = RoofIo::HoldReason::Moving; // the caller names "not closed" itself
            return token;
        }

        RoofIo::HoldReason keepUpload(uint32_t token)
        {
            return underLock([token]() { FlashGuard::progress(token); });
        }

        RestartGo commitRestart(bool mandatory, const std::function<bool()> &commit)
        {
            const RoofIo::PlatformAction action = mandatory ? RoofIo::PlatformAction::MandatoryRestart : RoofIo::PlatformAction::Restart;
            if (sequenceHold(action) != RoofIo::HoldReason::None)
                return RestartGo::Held;
            bool ran = false;
            bool committed = false;
            underLock(
                [&]()
                {
                    ran = true;
                    committed = commit();
                },
                false);
            if (!ran)
                return RestartGo::Held;
            return committed ? RestartGo::Run : RestartGo::NotDue;
        }
    } // namespace PlatformGuard
} // namespace SQM
