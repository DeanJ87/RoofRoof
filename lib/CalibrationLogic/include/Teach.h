#pragma once

// Spec 019: jog and teach the ends (research R1-R8). A person at the roof jogs
// it (Roof::Controller::jog, renewed by each keep-alive) and says where it is:
// closed, open or part-way. Each mark takes the Hall reading over the last
// second at rest; with all three the teach proposes which end reads high and
// the two thresholds (fitHall, the calibration run's rule, at the halfway
// points). Nothing is saved here: the caller saves a confirmed proposal
// through the settings save. Pure; RoofControl and the demo's roof step it
// right after the controller's tick, under the same lock.

#include <ArduinoJson.h>
#include <cstddef>
#include <cstdint>

#include "CalibrationProposal.h"
#include "CalibrationRun.h"
#include "CalibrationStats.h"
#include "RoofController.h"
#include "RoofTypes.h"

namespace SQM
{
    namespace Calib
    {
        // The reading window: one second of control steps (RoofIo::CONTROL_PERIOD_MS).
        constexpr uint8_t REST_READINGS = 50;
        constexpr uint8_t MARK_COUNT = 3; // indexed by Where

        struct TeachProposal
        {
            bool made = false; // all three marks are taken
            bool ok = false;   // and the values below fit; else `problem` says why
            Roof::End highEnd = Roof::End::Open;
            uint16_t highCounts = 0;
            uint16_t lowCounts = 0;
            Problem problem;
        };

        // The halfway point between the part-way reading and each end's, kept
        // in the window fitHall allows.
        TeachProposal proposeTeach(const Stats &closed, const Stats &open, const Stats &partWay);

        // What GET /api/roof/teach shows: small enough to copy under the roof's lock.
        struct TeachReport
        {
            bool jogActive = false;
            uint32_t jogId = 0;
            Roof::End jogTarget = Roof::End::Closed;
            uint32_t jogElapsedMs = 0;
            uint32_t jogLimitMs = 0;
            bool restReady = false;
            double restMean = 0.0;
            bool marked[MARK_COUNT] = {};
            Stats marks[MARK_COUNT];
            TeachProposal proposal;
        };

        class Teach
        {
        public:
            // A person's jog toward `target`: the run's start checks
            // (attendedRefusal), then the controller's (Controller::checkJog).
            Answer jog(Roof::Controller &controller, Roof::End target, bool attended, bool calibrating, const StartInputs &in);
            // A keep-alive for jog `id`: `jogEnded` when it isn't the one running.
            static Answer hold(Roof::Controller &controller, uint32_t id);
            // After each tick: the window fills while the roof is at rest.
            void step(const Roof::Status &status, uint16_t counts);
            Answer mark(Where where, const Roof::Status &status, bool calibrating);
            // The values the person confirms must be the current proposal.
            Answer checkConfirm(Roof::End highEnd, uint16_t highCounts, uint16_t lowCounts) const;
            // Drops the marks (and so the proposal).
            void clear();
            TeachProposal proposal() const;
            TeachReport report(const Roof::Controller &controller) const;

        private:
            Stats window() const;

            uint16_t ring[REST_READINGS] = {};
            uint8_t filled = 0;
            uint8_t next = 0;
            Stats marks[MARK_COUNT];
            bool marked[MARK_COUNT] = {};
        };

        // Enough for the fullest document (three marks, a long problem text).
        constexpr size_t TEACH_DOCUMENT_CAPACITY = 1536;

        void writeTeachDocument(JsonObject out, const TeachReport &report);

        // POST /api/roof/jog: {"direction": "open"|"close", "attended": bool, "id": int (optional, >0)}.
        struct JogRequest
        {
            Roof::End target = Roof::End::Closed;
            bool attended = false;
            uint32_t id = 0; // 0: start a jog; else renew this one
        };
        bool parseJog(JsonVariantConst body, JogRequest &out, const char *&error);
        // POST /api/roof/teach/mark: {"at": "closed"|"open"|"partWay"}.
        bool parseMark(JsonVariantConst body, Where &out, const char *&error);
        // POST /api/roof/teach/confirm: {"hallHighEnd": "open"|"closed", "hallHighCounts": int, "hallLowCounts": int}.
        struct TeachValues
        {
            Roof::End highEnd = Roof::End::Open;
            uint16_t highCounts = 0;
            uint16_t lowCounts = 0;
        };
        bool parseConfirm(JsonVariantConst body, TeachValues &out, const char *&error);
    } // namespace Calib
} // namespace SQM
