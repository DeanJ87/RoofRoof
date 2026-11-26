#include "ParkSend.h"

namespace SQM
{
    namespace Safety
    {
        using Nina::ReadingStatus;

        bool ParkSender::request(uint64_t nowMs)
        {
            if (pending)
                return false;
            pending = true;
            slewSeen = false;
            askedAtMs = nowMs;
            return true;
        }

        void ParkSender::remember(const Nina::MountReading &r)
        {
            if (r.status != ReadingStatus::Good || (lastGoodKnown && r.takenAtMs < lastGoodAtMs))
                return;
            lastGoodKnown = true;
            lastGoodAtMs = r.takenAtMs;
            lastGoodSlewing = r.slewing;
        }

        uint64_t ParkSender::decidesFromMs() const
        {
            // Only a reading taken after the request, and after our own send
            // has had time to show, decides.
            const uint64_t settled = sentEver ? sentAtMs + PARK_SENT_SETTLE_MS : 0;
            return settled > askedAtMs ? settled : askedAtMs;
        }

        ParkSender::Mount ParkSender::judge(const Nina::MountReading &r, uint64_t nowMs) const
        {
            if (r.status == ReadingStatus::Good)
            {
                if (r.slewing)
                    return Mount::Slewing;
                return r.connected && r.atPark ? Mount::Parked : Mount::Still;
            }
            // Unreadable: as before (send), unless the last good reading is
            // still fresh and said slewing.
            const bool fresh = lastGoodKnown && nowMs >= lastGoodAtMs && nowMs - lastGoodAtMs <= NINA_FRESH_MS;
            return fresh && lastGoodSlewing ? Mount::Slewing : Mount::Still;
        }

        ParkSendEvent ParkSender::sendNow(ParkSendEvent event, uint64_t nowMs)
        {
            pending = false;
            sentEver = true;
            sentAtMs = nowMs;
            return event;
        }

        ParkSendEvent ParkSender::step(const ParkSendInputs &in)
        {
            remember(in.reading);
            if (!pending)
                return ParkSendEvent::None;
            const uint64_t now = in.nowMs;
            // Spec 043: a park is a slew, sent only with the roof at an end,
            // checked again here, after any wait.
            if (!in.roofAtEnd)
            {
                pending = false;
                return ParkSendEvent::Dropped;
            }
            // No address: as before (the sender records it as refused).
            if (!in.ninaSet)
                return sendNow(ParkSendEvent::Send, now);
            const bool atLimit = now - askedAtMs >= PARK_SLEW_WAIT_MS;
            if (in.reading.takenAtMs < decidesFromMs())
                return atLimit ? sendNow(ParkSendEvent::SendAtLimit, now) : ParkSendEvent::None;
            switch (judge(in.reading, now))
            {
            case Mount::Parked:
                pending = false;
                return ParkSendEvent::AlreadyParked;
            case Mount::Still:
                return sendNow(ParkSendEvent::Send, now);
            case Mount::Slewing:
                break;
            }
            if (atLimit)
                return sendNow(ParkSendEvent::SendAtLimit, now);
            if (slewSeen)
                return ParkSendEvent::None;
            slewSeen = true;
            return ParkSendEvent::Waiting;
        }
    } // namespace Safety
} // namespace SQM
