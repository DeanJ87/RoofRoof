#pragma once

// The close sequence and the roof as alert events (spec 007). The sequence's
// critical decisions become typed alerts on spec 005's path (past the pause,
// the grace and the master switch, sent again until a channel takes them);
// the roof's state becomes the alert engine's inputs for the other events.
// Pure: nothing here feeds back into the sequence, the verdict or the roof
// (constitution III).

#include <cstdint>
#include <string>

#include "AlertEngine.h"
#include "CloseSequence.h"
#include "Daylight.h"
#include "RoofStatus.h"
#include "SafetyInput.h"

namespace SQM
{
    namespace CloseSeq
    {
        // The alert type of a critical event (FR-002); false for the others.
        bool alertTypeFor(Event event, Alerts::AlertType &type);

        // A critical decision as an alert: spec 005's title and message, the
        // "wake" level (the settings may lower it to urgent, no further), and
        // the variables `reason` (the park result's, fault's or refusal's text,
        // or the cause), `limit` (the time limit that ran out, or ""),
        // `details` (the whole built-in message) and `reminder` ("yes"/"no").
        // `why` is reasonFor()'s text. Only for isCritical(decision.event).
        Alerts::Alert criticalAlert(const Decision &decision, const std::string &why);
        // Spec 027: an Open or Close that waited for the park and gave up
        // (RoofIo::ParkWait): "mount not parked", with its text.
        Alerts::Alert parkWaitAlert(const std::string &why);
        // Spec 044: a person's stop lapsed (Event::HoldLapsed) and the close goes
        // ahead: "Hold lapsed: closing because it's still raining". The roof is
        // about to move by itself after a person stopped it, so it goes out as
        // the roof-not-closed event (critical: past the pause, never below
        // Urgent), as a report that stays queued until a channel takes it.
        // `weather` is the verdict's text.
        Alerts::Alert holdLapsedAlert(const Decision &decision, const std::string &weather);
        // Spec 039: a person's Open or Close set the park check aside
        // (RoofIo::parkOverrideText): an information alert at the normal level.
        Alerts::Alert parkOverrideAlert(const std::string &text);

        // The sequence is attempting a close: waiting for the park, closing,
        // or waiting to try again (not when it can't move or is stood down).
        bool attempting(Phase phase);

        // Whether a step's event is the roof closing after an attempt, a
        // person's stop or a halted close: the "roof closed after unsafe
        // weather" alert. A magnet that flickers back to closed while the
        // sequence can't move (CannotMove) doesn't count.
        bool closedAfterAttempt(Phase before, Event event);

        // Whether every weather source that is on has answered since the start
        // (none is still waiting for a first reply).
        bool sourcesKnown(const Safety::WeatherVerdict &weather);

        // What the sequence knows besides the roof and the weather.
        struct SequenceView
        {
            Phase phase = Phase::Idle;
            uint32_t closedCount = 0; // Event::Closed seen since start (weather episodes)
            // Spec 013: the episode is daylight-only (Sequence::daylightEpisode()).
            bool daylight = false;
            uint32_t daylightClosedCount = 0; // Event::Closed seen at the end of daylight episodes
        };

        // Refreshes `roof` from the roof's snapshot, the weather verdict and
        // the sequence. The weather text is taken while a weather episode runs
        // and kept after, so the close's alert says what the weather was.
        // "Closing" is a weather episode attempting a close (spec 013: a
        // daylight-only episode raises the daylight events instead).
        void updateConditions(
            Alerts::RoofInputs &roof,
            const RoofIo::Snapshot &snapshot,
            const Safety::WeatherVerdict &weather,
            const SequenceView &sequence);

        // Every weather source that is on and lost, with why ("" when none):
        // SQMeter stale, unreachable, unreadable or offline. Waiting for a first reply and switched off don't count.
        std::string lostSources(const Safety::WeatherVerdict &weather);

        // Spec 013: the daylight events' conditions. Open in daylight: the
        // mode on, the sun up, the roof not closed and no weather episode
        // running (its "Unsafe: closing" said it already). Unknown: the mode
        // on and the check unable to run. The sun's text is kept while a
        // daylight episode runs, for its "closed" alert.
        void updateDaylightConditions(
            Alerts::RoofInputs &roof, const Daylight::View &daylight, Roof::State state, const SequenceView &sequence);

        // Spec 027: N.I.N.A. lost. With an address set (`ninaSet`), the park
        // result says it is unreachable, unreadable, an API error or stale;
        // the text is the park result's.
        void updateNinaConditions(Alerts::RoofInputs &roof, const Safety::ParkResult &park, bool ninaSet);

        // The daylight state as the status document shows it.
        RoofIo::DaylightStatus daylightStatus(const Daylight::View &daylight);
    } // namespace CloseSeq
} // namespace SQM
