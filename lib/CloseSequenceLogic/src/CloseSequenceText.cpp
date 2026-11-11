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

            std::string seconds(uint64_t ms)
            {
                return std::to_string((ms + 999) / 1000) + " s";
            }

            std::string closeFailedMessage(const Decision &decision, const std::string &why)
            {
                switch (decision.cause)
                {
                case Cause::Fault:
                    return withWhy(
                        "The roof faulted while closing and stays where it stopped until a person clears the fault. Fault:", why);
                case Cause::Refused:
                    return withWhy("The roof refused to close; it tries again in 5 minutes. Refused:", why);
                case Cause::TooLong:
                    return withWhy(
                        "The close didn't finish " +
                            (decision.limitMs == 0 ? std::string("in time") : "within " + seconds(decision.limitMs)) +
                            " and was stopped where it is; it won't close by itself until the roof has been closed or the weather has been "
                            "safe for 10 minutes. Roof:",
                        why);
                default:
                    break;
                }
                return withWhy("The close stopped before the roof was closed; it tries again in 5 minutes. Roof:", why);
            }

            AlertText parkMessage(const Decision &decision, const std::string &why)
            {
                if (decision.cause == Cause::NinaNotConfigured)
                    return {
                        "Mount didn't park",
                        "The weather is unsafe and N.I.N.A. isn't set up (park.nina.url), so the park can't be "
                        "confirmed. The roof stays open."};
                return {
                    "Mount didn't park",
                    withWhy(
                        "The weather is unsafe and the park wasn't confirmed " +
                            (decision.limitMs == 0 ? std::string("in time") : "within " + seconds(decision.limitMs)) +
                            ", so the roof stays open. Park:",
                        why)};
            }

            AlertText firstAlertFor(const Decision &decision, const std::string &why)
            {
                switch (decision.event)
                {
                case Event::ParkNotConfirmed:
                    return parkMessage(decision, why);
                case Event::CloseFailed:
                    return {"Roof didn't close", closeFailedMessage(decision, why)};
                case Event::CannotClose:
                    if (decision.cause == Cause::RoofNotStarted)
                        return {
                            "Roof can't close by itself",
                            "The weather is unsafe and the roof controller didn't start, so the motor stays off. Restart the device."};
                    if (decision.cause == Cause::PositionUnknown)
                        return {
                            "Roof can't close by itself",
                            "The weather is unsafe and the roof's position is unknown. A person must move it to a magnet."};
                    return {"Roof can't close by itself", withWhy("The weather is unsafe and a fault is latched. Fault:", why)};
                case Event::StoppedByPerson:
                    return {
                        "Automatic close stopped",
                        "A person stopped the roof during the automatic close. It stays open while unsafe until someone closes it or the "
                        "weather has been safe for 10 minutes."};
                default:
                    break;
                }
                return {eventName(decision.event), why};
            }
        } // namespace

        std::string reasonFor(const Decision &decision, const Reasons &reasons)
        {
            switch (decision.cause)
            {
            case Cause::ParkTimeout:
            case Cause::NinaNotConfigured:
                return reasons.park;
            case Cause::Fault:
            case Cause::FaultLatched:
                return reasons.fault;
            case Cause::Refused:
                return reasons.refusal;
            case Cause::Interrupted:
            case Cause::TooLong:
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
                "none", "started", "endedSafe", "closed", "parkNotConfirmed", "closeFailed", "cannotClose", "stoppedByPerson"};
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
                "roofNotStarted"};
            const size_t index = static_cast<size_t>(cause);
            return index < sizeof(NAMES) / sizeof(NAMES[0]) ? NAMES[index] : "?";
        }
    } // namespace CloseSeq
} // namespace SQM
