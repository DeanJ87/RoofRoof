#include "ParkWait.h"

namespace SQM
{
    namespace RoofIo
    {
        void ParkWait::start(Roof::Command command, Roof::Origin origin, uint8_t overrides, uint32_t nowMs, const ParkWaitSettings &given)
        {
            active = true;
            cmd = command;
            from = origin;
            carried = overrides;
            settings = given;
            startedMs = nowMs;
            requested = false;
            parkedSeen = false;
            lastFailed = false;
            ended = ParkWaitEnd::None;
            text.clear();
        }

        void ParkWait::finish(ParkWaitEnd end, bool failure, const std::string &why)
        {
            active = false;
            ended = end;
            lastFailed = failure;
            text = why;
        }

        ParkWaitStep ParkWait::step(bool parked, uint32_t nowMs)
        {
            ParkWaitStep out;
            if (!active)
                return out;
            if (!requested || nowMs - lastRequestMs >= settings.gapMs)
            {
                out.requestPark = true;
                requested = true;
                lastRequestMs = nowMs;
            }
            if (!parked)
                parkedSeen = false;
            else if (!parkedSeen)
            {
                parkedSeen = true;
                parkedSinceMs = nowMs;
            }
            if (parkedSeen && nowMs - parkedSinceMs >= settings.holdMs)
            {
                finish(ParkWaitEnd::Ran, false, "");
                out.end = ParkWaitEnd::Ran;
                out.requestPark = false;
                return out;
            }
            if (nowMs - startedMs >= settings.limitMs)
            {
                finish(ParkWaitEnd::GaveUp, true, parkWaitGaveUpText(cmd, settings.limitMs));
                out.end = ParkWaitEnd::GaveUp;
                out.requestPark = false;
            }
            return out;
        }

        void ParkWait::cancel(const std::string &why)
        {
            if (active)
                finish(ParkWaitEnd::Cancelled, false, why);
            else
                lastFailed = false; // a new command: the last failure is no longer the news
        }

        void ParkWait::refuse(const std::string &why)
        {
            if (active || ended == ParkWaitEnd::Ran)
                finish(ParkWaitEnd::Refused, true, why);
        }

        uint32_t ParkWait::leftMs(uint32_t nowMs) const
        {
            if (!active)
                return 0;
            const uint32_t gone = nowMs - startedMs;
            return gone >= settings.limitMs ? 0 : settings.limitMs - gone;
        }

        const char *commandWord(Roof::Command command)
        {
            return command == Roof::Command::Open ? "Open" : "Close";
        }

        std::string parkWaitGaveUpText(Roof::Command command, uint32_t limitMs)
        {
            return "The mount didn't park within " + std::to_string(limitMs / 1000) + " s: the " + commandWord(command) + " wasn't run";
        }

        std::string parkWaitImpossible(bool ninaSet, bool requestPark)
        {
            if (!ninaSet)
                return "no N.I.N.A. address to ask for the park: park the mount, or set N.I.N.A.'s address";
            if (!requestPark)
                return "asking N.I.N.A. to park is off (Settings, Roof, N.I.N.A.): park the mount, or turn it on";
            return "";
        }
    } // namespace RoofIo
} // namespace SQM
