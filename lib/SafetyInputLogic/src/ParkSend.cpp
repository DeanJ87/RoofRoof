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
            // After the stop at the limit, only a reading taken after it.
            const uint64_t settled = sentEver ? sentAtMs + PARK_SENT_SETTLE_MS : 0;
            uint64_t from = settled > askedAtMs ? settled : askedAtMs;
            if (stopSent && stopAtMs + 1 > from)
                from = stopAtMs + 1;
            return from;
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
            if (in.reading.takenAtMs < decidesFromMs())
                return stillSlewing(now);
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
            const ParkSendEvent limit = stillSlewing(now);
            if (limit != ParkSendEvent::None || slewSeen)
                return limit;
            slewSeen = true;
            return ParkSendEvent::Waiting;
        }

        ParkSendEvent ParkSender::stillSlewing(uint64_t nowMs)
        {
            // M06: a park sent into a slew fails (the API stops it and parks at
            // once; OnStep refuses while decelerating). At the limit the slew
            // is stopped on its own, and the park waits for the mount to be still.
            if (!stopSent)
            {
                if (nowMs - askedAtMs < PARK_SLEW_WAIT_MS)
                    return ParkSendEvent::None;
                stopSent = true;
                slewSeen = true;
                stopAtMs = nowMs;
                return ParkSendEvent::StopSlew;
            }
            if (nowMs - stopAtMs < PARK_STOP_WAIT_MS)
                return ParkSendEvent::None;
            return sendNow(ParkSendEvent::SendAtLimit, nowMs);
        }
    } // namespace Safety
} // namespace SQM
