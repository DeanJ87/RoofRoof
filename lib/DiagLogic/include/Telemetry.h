#pragma once

// Spec 046: long-term roof telemetry over MQTT (for InfluxDB through
// Telegraf). <base>/telemetry: about once a second while the roof moves (and
// TAIL_MS after it stops), otherwise only the supply voltage every
// mqtt.telemetry.restVoltsSeconds or sooner on a VOLTS_CHANGE step.
// <base>/move: one record per finished move, from the diagnostics move log
// (spec 017). Not retained, QoS 0; a record that can't go is dropped (the
// device keeps its last moves in /api/diag/moves). The documents are written
// into a caller's fixed buffer: no heap, no JSON pool.

#include <cstddef>
#include <cstdint>

#include "DiagTypes.h"
#include "RoofTypes.h"

namespace SQM
{
    namespace Telemetry
    {
        constexpr const char *SUBTOPIC = "telemetry";
        constexpr const char *MOVE_SUBTOPIC = "move";
        // While the roof moves, and for TAIL_MS after it stops (to catch the end).
        constexpr uint32_t MOVING_INTERVAL_MS = 1000;
        constexpr uint32_t TAIL_MS = 3000;
        // At rest: a change of the supply this big goes out before the interval,
        // but never sooner than VOLTS_CHANGE_MIN_MS after the last publish (a
        // noisy reading can't make it more than a sample every 10 s).
        constexpr float VOLTS_CHANGE = 0.2f;
        constexpr uint32_t VOLTS_CHANGE_MIN_MS = 10000;
        // The buffers the documents are written into, sized by test_telemetry
        // (every field at its longest).
        constexpr size_t SAMPLE_BYTES = 256;
        constexpr size_t MOVE_BYTES = 512;

        // The roof now, as the telemetry document shows it.
        struct Sample
        {
            Roof::State state = Roof::State::Unknown;
            bool positionKnown = false;
            float positionPercent = 0.0f; // 0 closed, 100 open
            uint16_t hall = 0;            // the Hall sensor's raw counts
            float volts = NAN;            // the supply; NaN when unreadable
            uint8_t dutyPercent = 0;      // the PWM duty applied now
            const char *phase = "idle";   // the automatic close's phase (CloseSeq::phaseName)
            Roof::End target = Roof::End::Closed;
            Roof::Origin origin = Roof::Origin::Person;
        };

        // The document's time: Unix milliseconds once the clock is set,
        // otherwise the uptime in milliseconds (below 1e12, so a reader can
        // tell them apart).
        struct Stamp
        {
            uint64_t ms = 0;
            bool epoch = false;
        };
        // The stamp of a moment `atMs` (uptime) given the clock now: Unix
        // milliseconds `epochMs` (valid from Core::CLOCK_VALID_EPOCH; 0 or
        // less when the clock isn't set) at uptime `nowMs`.
        Stamp stampOf(int64_t epochMs, uint32_t nowMs, uint32_t atMs);

        // Which telemetry document is due.
        enum class Kind : uint8_t
        {
            None,
            Moving, // the full sample
            Rest,   // {"ts","volts"}
        };

        class Schedule
        {
        public:
            // mqtt.telemetry: off publishes nothing; restSeconds 0: nothing at rest.
            void configure(bool enabled, uint32_t restSeconds)
            {
                on = enabled;
                restMs = restSeconds * 1000u;
            }
            // `moving`: the roof is opening or closing now.
            Kind due(bool moving, float volts, uint32_t nowMs);
            // Called after every attempt, sent or not: a document that couldn't
            // go is dropped, never queued.
            void published(Kind kind, float volts, uint32_t nowMs);

        private:
            bool on = true;
            uint32_t restMs = 60000;
            bool any = false;      // a publish since start
            uint32_t lastMs = 0;   // the last publish, either kind
            float lastVolts = NAN; // its supply
            bool moveSeen = false; // moving now or within TAIL_MS
            uint32_t lastMovingMs = 0;
            bool moveStarted = false; // the move's first sample is still to go
        };

        // A reading that can't be read is null (Telegraf skips it). The
        // writers return the length written (no terminator counted), or 0 when
        // `size` is too small (then nothing is published).
        size_t writeSample(char *out, size_t size, const Sample &sample, const Stamp &stamp);
        size_t writeRest(char *out, size_t size, float volts, const Stamp &stamp);
        // A finished move's record (Diag::Summary). `currentSense`: peakAmps and
        // averageAmps are written only with a current sensor fitted.
        size_t writeMove(char *out, size_t size, const Diag::Summary &move, const Stamp &stamp, bool currentSense);

        // Which finished move to publish: the newest finished one, once, on
        // the MQTT connection it finished on. The first look only primes
        // (nothing from before), and so does a new connection (a move that
        // finished while MQTT was down is dropped, never queued).
        class MoveCue
        {
        public:
            // `finished`: the move log's finished count; true when a new move ended.
            bool newMove(uint32_t finished, uint32_t connection)
            {
                const bool fresh = primed && connection == lastConnection && finished != lastFinished;
                lastFinished = finished;
                lastConnection = connection;
                primed = true;
                return fresh;
            }

        private:
            bool primed = false;
            uint32_t lastFinished = 0;
            uint32_t lastConnection = 0;
        };
    } // namespace Telemetry
} // namespace SQM
