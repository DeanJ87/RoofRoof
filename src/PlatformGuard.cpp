#include "PlatformGuard.h"

#include <atomic>

#include <WiFi.h>

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
            std::function<RoofIo::SequenceState()> sequenceProbe;
            std::function<bool()> committedProbe;

            RoofIo::SequenceState sequenceNow()
            {
                return sequenceProbe ? sequenceProbe() : RoofIo::SequenceState::Idle;
            }

            // The home network joined and the setup hotspot off: only then can
            // a working close sequence reach N.I.N.A. (spec 010).
            bool networkUp()
            {
                return WiFi.status() == WL_CONNECTED && (WiFi.getMode() & WIFI_AP) == 0;
            }

            RoofIo::PlatformState latest()
            {
                RoofIo::PlatformState state;
                if (theRoof != nullptr)
                {
                    state.roof = theRoof->state();
                    state.closeWaiting = theRoof->closeWaiting();
                    state.calibrating = theRoof->calibrating(); // spec 016: refused up front too
                }
                else
                {
                    state.roof = Roof::State::Closed; // no roof: nothing moves
                }
                state.sequence = sequenceNow();
                state.networkUp = networkUp();
                return state;
            }

            // The close sequence's part of the rule (the roof's is checked under its lock).
            RoofIo::HoldReason sequenceHold(RoofIo::PlatformAction action)
            {
                RoofIo::PlatformState state;
                state.roof = Roof::State::Closed;
                state.sequence = sequenceNow();
                state.networkUp = networkUp();
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

            // The roof's part checked under its lock, and the close sequence's
            // checked again inside it (the sequence may have left idle since a
            // first look): `action` runs only if nothing holds `platform`.
            RoofIo::HoldReason underLockFor(RoofIo::PlatformAction platform, const std::function<void()> &action)
            {
                RoofIo::HoldReason reason = sequenceHold(platform);
                if (reason != RoofIo::HoldReason::None)
                    return reason;
                const RoofIo::HoldReason roof = underLock(
                    [&]()
                    {
                        reason = sequenceHold(platform);
                        if (reason == RoofIo::HoldReason::None)
                            action();
                    });
                return roof != RoofIo::HoldReason::None ? roof : reason;
            }
        } // namespace

        void begin(RoofControl *roof, std::function<RoofIo::SequenceState()> busy, std::function<bool()> restartCommitted)
        {
            theRoof = roof;
            sequenceProbe = std::move(busy);
            committedProbe = std::move(restartCommitted);
        }

        RoofIo::SequenceState sequence()
        {
            return sequenceNow();
        }

        RoofIo::HoldReason hold(RoofIo::PlatformAction action)
        {
            return RoofIo::platformHold(action, latest());
        }

        RoofIo::HoldReason claim(RoofIo::PlatformAction action, const std::function<void()> &mark)
        {
            return underLockFor(action, mark);
        }

        FlashWrite::FlashWrite(RoofIo::PlatformAction action)
        {
            reason = underLockFor(
                action,
                [this]()
                {
                    // A committed restart is moments away: don't start a write
                    // it would cut off (reported as the roof being busy; retried).
                    if (committedProbe && committedProbe())
                    {
                        refusedForRestart = true;
                        return;
                    }
                    FlashGuard::holdShort();
                    held = true;
                });
        }

        FlashWrite::~FlashWrite()
        {
            if (held)
                FlashGuard::releaseShort();
        }

        RoofIo::HoldReason FlashWrite::refused() const
        {
            return refusedForRestart ? RoofIo::HoldReason::Moving : reason;
        }

        uint32_t beginUpload(RoofIo::PlatformAction action, RoofIo::HoldReason &why, bool closedOnly)
        {
            uint32_t token = 0;
            bool notClosed = false;
            why = underLockFor(
                action,
                [&]()
                {
                    if (committedProbe && committedProbe())
                    {
                        notClosed = true; // reported as busy; the reset is moments away
                        return;
                    }
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
            bool committed = false;
            const RoofIo::HoldReason reason = underLock(
                [&]()
                {
                    committed = committedProbe && committedProbe();
                    if (!committed)
                        FlashGuard::progress(token);
                });
            return committed ? RoofIo::HoldReason::Moving : reason;
        }

        RestartGo commitRestart(bool mandatory, const std::function<bool()> &commit)
        {
            bool ran = false;
            bool held = false;
            bool committed = false;
            underLock(
                [&]()
                {
                    ran = true;
                    RoofIo::RestartInputs in;
                    in.mandatory = mandatory;
                    in.sequenceWorking = sequenceNow() == RoofIo::SequenceState::Working && networkUp();
                    in.uploadBusy = FlashGuard::busy();
                    in.shortWrite = FlashGuard::writing();
                    in.faultUnsaved = theRoof != nullptr && theRoof->faultUnsavedHeld();
                    if (!RoofIo::restartMayCommit(in))
                    {
                        held = true;
                        return;
                    }
                    committed = commit();
                },
                false);
            if (committed)
                return RestartGo::Run;
            return !ran || held ? RestartGo::Held : RestartGo::NotDue;
        }
    } // namespace PlatformGuard
} // namespace SQM
