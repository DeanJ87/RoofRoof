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
            uint32_t closedCount = 0; // Event::Closed seen since start
        };

        // Refreshes `roof` from the roof's snapshot, the weather verdict and
        // the sequence. The weather text is taken while the sequence runs and
        // kept after, so the close's alert says what the weather was.
        void updateConditions(
            Alerts::RoofInputs &roof,
            const RoofIo::Snapshot &snapshot,
            const Safety::WeatherVerdict &weather,
            const SequenceView &sequence);

        // Every weather source that is on and lost, with why ("" when none):
        // SQMeter stale, unreachable, unreadable or offline; the rain contact
        // unreadable. Waiting for a first reply and switched off don't count.
        std::string lostSources(const Safety::WeatherVerdict &weather);
    } // namespace CloseSeq
} // namespace SQM
