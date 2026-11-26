#include "RoofAlerts.h"

#include "RoofText.h"

namespace SQM
{
    namespace CloseSeq
    {
        namespace
        {
            // When the decision's own reason is empty, what the cause says.
            std::string causeText(const Decision &decision)
            {
                if (decision.event == Event::StoppedByPerson)
                    return decision.remoteStop ? "Stopped by an imaging app or MQTT" : "Stopped by a person";
                switch (decision.cause)
                {
                case Cause::PositionUnknown:
                    return "Position unknown";
                case Cause::RoofNotStarted:
                    return "Roof controller not started";
                case Cause::NinaNotConfigured:
                    return "N.I.N.A. not configured";
                case Cause::WasUnknown:
                    return "Closed from an unknown position";
                case Cause::NinaLost:
                    return "Closed without N.I.N.A.'s park";
                default:
                    break;
                }
                return "";
            }

            bool lost(const Safety::SourceStatus &source)
            {
                switch (source.state)
                {
                case Safety::SourceState::Stale:
                case Safety::SourceState::Unreachable:
                case Safety::SourceState::Unreadable:
                case Safety::SourceState::Offline:
                    return true;
                default:
                    break;
                }
                return false;
            }

            void append(std::string &text, const std::string &part)
            {
                if (part.empty())
                    return;
                if (!text.empty())
                    text += "; ";
                text += part;
            }
        } // namespace

        bool alertTypeFor(Event event, Alerts::AlertType &type)
        {
            switch (event)
            {
            case Event::ParkNotConfirmed:
                type = Alerts::AlertType::MountNotParked;
                return true;
            case Event::CloseFailed:
            case Event::StoppedByPerson:
                type = Alerts::AlertType::RoofNotClosed;
                return true;
            case Event::CannotClose: // spec 030: the roof can't move by itself: a person must act
            case Event::NeedsPerson: // spec 030: a close ran without a known position or N.I.N.A.'s park
                type = Alerts::AlertType::NeedsPerson;
                return true;
            default:
                break;
            }
            return false;
        }

        Alerts::Alert criticalAlert(const Decision &decision, const std::string &why)
        {
            const AlertText text = alertFor(decision, why);
            Alerts::Alert alert;
            if (!alertTypeFor(decision.event, alert.type))
                alert.type = Alerts::AlertType::RoofNotClosed; // not reached: only critical events come here
            alert.level = Alerts::AlertLevel::Wake;
            alert.title = text.title;
            alert.message = text.message;
            alert.vars = {
                {"reason", why.empty() ? causeText(decision) : why},
                {"limit", decision.limitMs == 0 ? std::string() : std::to_string((decision.limitMs + 999) / 1000) + " s"},
                {"details", text.message},
                {"reminder", decision.reminder ? "yes" : "no"},
            };
            return alert;
        }

        Alerts::Alert parkWaitAlert(const std::string &why)
        {
            Alerts::Alert alert;
            alert.type = Alerts::AlertType::MountNotParked;
            alert.level = Alerts::AlertLevel::Wake;
            alert.title = "Mount not parked";
            alert.message = why + ". N.I.N.A. was asked to park; the roof has not moved.";
            alert.vars = {{"reason", why}, {"limit", ""}, {"details", alert.message}};
            return alert;
        }

        Alerts::Alert holdLapsedAlert(const Decision &decision, const std::string &weather)
        {
            const AlertText text = alertFor(decision, weather);
            Alerts::Alert alert;
            alert.type = Alerts::AlertType::RoofNotClosed;
            alert.level = Alerts::AlertLevel::Wake;
            alert.title = text.title;
            alert.message = text.message;
            alert.vars = {
                {"reason", decision.lapsedInRain ? "Hold lapsed: still raining" : "Hold lapsed: still unsafe"},
                {"limit", std::to_string((decision.stopHoldMs + 59999) / 60000) + " min"},
                {"details", text.message},
                {"reminder", "no"},
            };
            return alert;
        }

        Alerts::Alert parkOverrideAlert(const std::string &text)
        {
            Alerts::Alert alert;
            alert.type = Alerts::AlertType::CheckOverridden;
            alert.level = Alerts::AlertLevel::Normal;
            alert.title = "Park check overridden";
            alert.message = text + ".";
            alert.vars = {{"reason", text}, {"details", alert.message}};
            return alert;
        }

        bool closedAfterAttempt(Phase before, Event event)
        {
            // A person closing the roof after a stop or a halted close counts
            // too. Spec 030: closed straight from a wait (no close seen) is a
            // closed magnet flickering back at rest (an unknown position now
            // starts an attempt), as in CannotMove: it doesn't count.
            return event == Event::Closed && (before == Phase::Closing || before == Phase::HeldByPerson || before == Phase::Halted);
        }

        bool sourcesKnown(const Safety::WeatherVerdict &weather)
        {
            return weather.http.state != Safety::SourceState::Waiting && weather.mqtt.state != Safety::SourceState::Waiting;
        }

        bool attempting(Phase phase)
        {
            return phase == Phase::WaitPark || phase == Phase::Closing || phase == Phase::RetryWait;
        }

        std::string lostSources(const Safety::WeatherVerdict &weather)
        {
            std::string text;
            if (lost(weather.http))
                append(text, Safety::describeSource(weather.http));
            if (lost(weather.mqtt))
                append(text, Safety::describeSource(weather.mqtt));
            return text;
        }

        void updateConditions(
            Alerts::RoofInputs &roof, const RoofIo::Snapshot &snapshot, const Safety::WeatherVerdict &weather, const SequenceView &sequence)
        {
            roof.known = true;
            roof.faultLatched = snapshot.fault.fault.kind != Roof::FaultKind::None;
            roof.fault = roof.faultLatched ? RoofIo::describeFault(snapshot.fault) : std::string();
            roof.supplyLow = RoofIo::supplyLow(snapshot);
            roof.supply = roof.supplyLow ? RoofIo::describeSupply(snapshot.supplyVolts, snapshot.minSupplyVolts) : std::string();
            roof.source = lostSources(weather);
            roof.sourceLost = !roof.source.empty();
            roof.sourceKnown = sourcesKnown(weather) || roof.sourceLost;
            roof.closing = attempting(sequence.phase) && !sequence.daylight;
            if (sequence.phase != Phase::Idle && !sequence.daylight)
                roof.weather = Safety::describe(weather);
            roof.closedCount = sequence.closedCount;
        }

        void updateDaylightConditions(
            Alerts::RoofInputs &roof, const Daylight::View &daylight, Roof::State state, const SequenceView &sequence)
        {
            const bool weatherRunning = sequence.phase != Phase::Idle && !sequence.daylight;
            const bool daylightClosing = attempting(sequence.phase) && sequence.daylight;
            roof.daylightOpen = daylight.on() && daylight.state == Daylight::State::Day && state != Roof::State::Closed && !weatherRunning;
            roof.daylightSun = roof.daylightOpen ? Daylight::describe(daylight) : std::string();
            roof.daylightAction = !roof.daylightOpen ? std::string()
                                  : daylight.bypassed && daylight.mode == Daylight::Mode::Close
                                      ? "Its close is bypassed for a test box."
                                      : Daylight::actionText(daylight.mode, daylightClosing);
            roof.daylightUnknown = daylight.on() && daylight.state == Daylight::State::Unknown;
            roof.daylightMissing = roof.daylightUnknown ? Daylight::describe(daylight) : std::string();
            if (sequence.phase != Phase::Idle && sequence.daylight)
                roof.daylightClosedSun = Daylight::describe(daylight);
            roof.daylightClosedCount = sequence.daylightClosedCount;
        }

        void updateNinaConditions(Alerts::RoofInputs &roof, const Safety::ParkResult &park, bool ninaSet)
        {
            constexpr uint32_t LOST =
                Safety::ParkNinaUnreachable | Safety::ParkNinaUnreadable | Safety::ParkNinaApiError | Safety::ParkNinaStale;
            roof.ninaLost = ninaSet && (park.reasons & LOST) != 0;
            roof.ninaLostText = roof.ninaLost ? Safety::describe(park) : std::string();
        }

        RoofIo::DaylightStatus daylightStatus(const Daylight::View &daylight)
        {
            RoofIo::DaylightStatus out;
            out.mode = Daylight::modeName(daylight.mode);
            out.state = Daylight::stateName(daylight.state);
            out.known = daylight.known;
            out.altitudeDeg = daylight.altitudeDeg;
            out.limitDeg = daylight.limitDeg;
            out.text = Daylight::describe(daylight);
            out.closes = daylight.closes();
            return out;
        }
    } // namespace CloseSeq
} // namespace SQM
