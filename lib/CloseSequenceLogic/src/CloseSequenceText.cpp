#include "CloseSequence.h"

// Names for logs and the critical alerts' wording. Spec 007 replaces the
// wording with its own events, levels and templates.

namespace SQM
{
    namespace CloseSeq
    {
        namespace
        {
            std::string withWhy(const std::string &text, const std::string &why)
            {
                return why.empty() ? text : text + " " + why;
            }

            // Why the roof should close: the weather (spec 005) or daylight (spec 013).
            const char *whyClose(const Decision &decision)
            {
                return decision.daylight ? "It is daylight" : "The weather is unsafe";
            }

            std::string seconds(uint64_t ms)
            {
                return std::to_string((ms + 999) / 1000) + " s";
            }

            // Spec 014: the policy's times in whole minutes (the settings are minutes).
            std::string minutes(uint64_t ms)
            {
                const uint64_t whole = (ms + 59999) / 60000;
                return std::to_string(whole) + (whole == 1 ? " minute" : " minutes");
            }

            // What re-arms a stood-down sequence besides a person's close.
            std::string rearmText(const Decision &decision)
            {
                return decision.daylight ? "the sun has been down (or the daylight close off) for " + minutes(decision.rearmMs) +
                                               ", or the weather has been unsafe for 30 s"
                                         : "the weather has been safe for " + minutes(decision.rearmMs);
            }

            // Spec 044: when a person's stop lapses by itself (autoClose.personStopLapse);
            // "" when it doesn't (the default, and a daylight close's stop).
            std::string lapseText(const Decision &decision)
            {
                if (decision.stopLapse == StopLapse::Never || decision.daylight)
                    return "";
                const std::string weather = decision.stopLapse == StopLapse::Rain ? "rain" : "unsafe weather";
                return " After " + minutes(decision.stopHoldMs) + " of " + weather +
                       " the hold lapses and the close goes ahead by itself; press Stop again to hold it longer.";
            }

            std::string triesAgain(const Decision &decision)
            {
                return "it tries again in " + minutes(decision.retryMs);
            }

            std::string closeFailedMessage(const Decision &decision, const std::string &why)
            {
                switch (decision.cause)
                {
                case Cause::Fault:
                    return withWhy(
                        "The roof faulted while closing and stays where it stopped until a person clears the fault. Fault:", why);
                case Cause::Refused:
                    return withWhy("The roof refused to close; " + triesAgain(decision) + ". Refused:", why);
                case Cause::NotStarted:
                    return withWhy(
                        "The close didn't start within " + seconds(decision.limitMs) + " and was called off; " + triesAgain(decision) +
                            ". Roof:",
                        why);
                case Cause::TooLong:
                    return withWhy(
                        "The close didn't finish " +
                            (decision.limitMs == 0 ? std::string("in time") : "within " + seconds(decision.limitMs)) +
                            " and was stopped where it is; it won't close by itself until the roof has been closed or " +
                            rearmText(decision) + ". Roof:",
                        why);
                default:
                    break;
                }
                return withWhy("The close stopped before the roof was closed; " + triesAgain(decision) + ". Roof:", why);
            }

            // Spec 030: the title names the input that is missing.
            const char *parkTitle(ParkGap gap)
            {
                switch (gap)
                {
                case ParkGap::NinaLost:
                    return "Can't close: N.I.N.A. unreachable, mount park unknown";
                case ParkGap::SourcesDisagree:
                    return "Can't close: N.I.N.A. and the park switches disagree";
                case ParkGap::None:
                    break;
                }
                return "Mount didn't park";
            }

            AlertText parkMessage(const Decision &decision, const std::string &why)
            {
                if (decision.cause == Cause::NinaNotConfigured)
                    return {
                        "Mount didn't park",
                        std::string(whyClose(decision)) + " and N.I.N.A. isn't set up (park.nina.url), so the park can't be "
                                                          "confirmed. The roof stays open."};
                return {
                    parkTitle(decision.parkGap),
                    withWhy(
                        std::string(whyClose(decision)) + " and the park wasn't confirmed " +
                            (decision.limitMs == 0 ? std::string("in time") : "within " + seconds(decision.limitMs)) +
                            ", so the roof stays open. Park:",
                        why)};
            }

            // Spec 030: a close that ran without a known position or N.I.N.A.'s park.
            AlertText needsPersonMessage(const Decision &decision, const std::string &why)
            {
                const std::string because = whyClose(decision);
                const std::string nina =
                    decision.ninaLost ? " It closed without N.I.N.A.'s park confirmation (autoClose.ninaLostClose)." : "";
                if (decision.cause == Cause::ClosedBlind)
                    return {
                        "Roof closed blind: check the roof",
                        withWhy(
                            because +
                                " and the Hall sensor is dead, so the roof was driven towards closed once, on time alone. "
                                "Whether it closed is unknown: a person must check the roof." +
                                nina + " Fault:",
                            why)};
                if (decision.cause == Cause::NinaLost)
                    return {
                        "Roof closed without N.I.N.A.'s park",
                        because + " and N.I.N.A. couldn't be read for the set time, so the roof closed without N.I.N.A.'s park "
                                  "confirmation (autoClose.ninaLostClose). A person must check the telescope and the roof."};
                return {
                    "Roof closed from an unknown position",
                    because +
                        " and the roof's position was unknown, so it closed slowly to the first magnet. A person must "
                        "check the roof." +
                        nina};
            }

            AlertText firstAlertFor(const Decision &decision, const std::string &why)
            {
                const std::string because = whyClose(decision);
                switch (decision.event)
                {
                case Event::ParkNotConfirmed:
                    return parkMessage(decision, why);
                case Event::CloseFailed:
                    return {"Roof didn't close", closeFailedMessage(decision, why)};
                case Event::CannotClose:
                    if (decision.cause == Cause::RoofBetweenEnds) // spec 043
                        return {
                            "Roof not at an end: mount not parked",
                            withWhy(
                                because + ". " +
                                    (why.empty() ? std::string("The roof isn't at an end with the mount not parked: moving the roof or "
                                                               "the telescope would hit the beams. A person must check.")
                                                 : why) +
                                    " RoofRoof doesn't ask N.I.N.A. to park or close the roof until the roof is at an end or the "
                                    "mount is parked.",
                                "")};
                    if (decision.cause == Cause::RoofNotStarted)
                        return {
                            "Roof can't close by itself",
                            because + " and the roof controller didn't start, so the motor stays off. Restart the device."};
                    if (decision.cause == Cause::PositionUnknown)
                        return {
                            "Roof can't close by itself",
                            because + " and the roof's position is unknown. A person must move it to a magnet."};
                    return {"Roof can't close by itself", withWhy(because + " and a fault is latched. Fault:", why)};
                case Event::NeedsPerson:
                    return needsPersonMessage(decision, why);
                case Event::HoldLapsed: // spec 044
                {
                    const char *still = decision.lapsedInRain ? "still raining" : "still unsafe";
                    return {
                        std::string("Hold lapsed: closing because it's ") + still,
                        withWhy(
                            "A person's Stop held the automatic close; after " + minutes(decision.stopHoldMs) + " of " +
                                (decision.stopLapse == StopLapse::Rain ? "rain" : "unsafe weather") +
                                " the hold lapsed (autoClose.personStopLapse), so the close goes ahead: the mount is asked "
                                "to park and the roof closes once the park is confirmed. Press Stop to hold it again. Weather:",
                            why)};
                }
                case Event::StoppedByPerson:
                    return {
                        "Automatic close stopped",
                        std::string(decision.remoteStop ? "An imaging app or MQTT" : "A person") +
                            " stopped the roof during the automatic close. It stays open " +
                            (decision.daylight ? "in daylight" : "while unsafe") + " until someone closes it or " + rearmText(decision) +
                            "." + lapseText(decision)};
                default:
                    break;
                }
                return {eventName(decision.event), why};
            }
        } // namespace

        void applyOverride(Inputs &in, uint8_t overrides, const Safety::WeatherVerdict &weather)
        {
            RoofIo::GateInputs gates;
            gates.weather = weather.verdict;
            gates.weatherReasons = weather.reasons;
            if ((overrides & RoofIo::OverrideWeather) != 0 && RoofIo::weatherOverridable(gates))
                in.weatherSafe = true;
            if ((overrides & RoofIo::OverrideDaylight) != 0)
                in.daylight = false;
        }

        std::string parkReason(const Safety::ParkResult &park, bool requestPark)
        {
            const std::string text = Safety::describe(park);
            return requestPark || park.parked ? text : text + " (N.I.N.A. not asked to park: park requests are off)";
        }

        ParkGap parkGapOf(const Safety::ParkResult &park)
        {
            constexpr uint32_t NINA_LOST = Safety::ParkNinaWaiting | Safety::ParkNinaUnreachable | Safety::ParkNinaUnreadable |
                                           Safety::ParkNinaApiError | Safety::ParkNinaStale;
            if (park.parked || park.bypassed)
                return ParkGap::None;
            if ((park.reasons & Safety::ParkSourcesDisagree) != 0)
                return ParkGap::SourcesDisagree;
            if (!park.switchesOnly && (park.reasons & NINA_LOST) != 0)
                return ParkGap::NinaLost;
            return ParkGap::None;
        }

        std::string reasonFor(const Decision &decision, const Reasons &reasons)
        {
            switch (decision.cause)
            {
            case Cause::ParkTimeout:
            case Cause::NinaNotConfigured:
                return reasons.park;
            case Cause::Fault:
            case Cause::FaultLatched:
            case Cause::ClosedBlind:
                return reasons.fault;
            case Cause::Refused:
                return reasons.refusal;
            case Cause::RoofBetweenEnds:
                return reasons.betweenEnds; // spec 043
            case Cause::Interrupted:
            case Cause::TooLong:
            case Cause::NotStarted:
                return Roof::stateName(reasons.roof);
            default:
                break;
            }
            return "";
        }

        AlertText alertFor(const Decision &decision, const std::string &why)
        {
            AlertText text = firstAlertFor(decision, why);
            if (decision.reminder)
                text.title = "Still: " + text.title;
            return text;
        }

        const char *phaseName(Phase phase)
        {
            static constexpr const char *NAMES[] = {"idle", "waitPark", "closing", "retryWait", "heldByPerson", "cannotMove", "halted"};
            const size_t index = static_cast<size_t>(phase);
            return index < sizeof(NAMES) / sizeof(NAMES[0]) ? NAMES[index] : "?";
        }

        const char *eventName(Event event)
        {
            static constexpr const char *NAMES[] = {
                "none",
                "started",
                "endedSafe",
                "closed",
                "parkNotConfirmed",
                "closeFailed",
                "cannotClose",
                "stoppedByPerson",
                "needsPerson",
                "holdLapsed"};
            const size_t index = static_cast<size_t>(event);
            return index < sizeof(NAMES) / sizeof(NAMES[0]) ? NAMES[index] : "?";
        }

        const char *causeName(Cause cause)
        {
            static constexpr const char *NAMES[] = {
                "none",
                "parkTimeout",
                "ninaNotConfigured",
                "fault",
                "refused",
                "interrupted",
                "tooLong",
                "positionUnknown",
                "faultLatched",
                "roofNotStarted",
                "closeNotStarted",
                "wasUnknown",
                "closedBlind",
                "ninaLost",
                "roofBetweenEnds"};
            const size_t index = static_cast<size_t>(cause);
            return index < sizeof(NAMES) / sizeof(NAMES[0]) ? NAMES[index] : "?";
        }
        const char *releaseLogText(Phase before, Phase after)
        {
            if (before != Phase::HeldByPerson && before != Phase::Halted)
                return "Release request dropped: the stand-down had already ended";
            return after == Phase::Idle ? "Stand-down released by a person at the roof"
                                        : "Release request dropped: the sequence checked again and kept its stand-down";
        }

        bool openEndedHold(Phase before, Phase after, Roof::State roof)
        {
            return before == Phase::HeldByPerson && after == Phase::Idle && roof == Roof::State::Opening;
        }

        const char *releaseRefusal(Phase phase, bool nothingWantsClose)
        {
            if (phase != Phase::HeldByPerson && phase != Phase::Halted)
                return "The automatic close isn't standing down: there is nothing to release";
            if (!nothingWantsClose)
                return "The weather or the daylight check wants the roof closed: the stand-down can't be released now";
            return nullptr;
        }
    } // namespace CloseSeq
} // namespace SQM
