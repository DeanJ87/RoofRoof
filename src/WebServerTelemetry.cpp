#include "WebServer.h"

#include <sys/time.h>

#include "MotionGuards.h"
#include "RoofControl.h"
#include "Telemetry.h"

// Spec 046: long-term telemetry over MQTT, for InfluxDB through Telegraf.
// <base>/telemetry about once a second while the roof moves (and for
// Telemetry::TAIL_MS after), only the supply at rest; <base>/move once per
// finished move, from the diagnostics move log. Both are written into fixed
// stack buffers (no heap, no JSON pool), not retained, and dropped when they
// can't go: nothing is queued. Main loop only, from publishRoofState's check
// (ten times a second, while MQTT is connected), after the state document.

namespace SQM
{
    namespace
    {
        int64_t epochMsNow()
        {
            struct timeval tv;
            if (gettimeofday(&tv, nullptr) != 0)
                return 0;
            return static_cast<int64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
        }

        Telemetry::Sample sampleOf(const RoofIo::Snapshot &s)
        {
            Telemetry::Sample out;
            out.state = s.status.state;
            out.positionKnown = s.status.positionKnown && s.travelMm > 0;
            out.positionPercent = out.positionKnown ? s.status.positionMm * 100.0f / static_cast<float>(s.travelMm) : 0.0f;
            out.hall = s.status.hallCounts;
            out.volts = s.supplyVolts;
            out.dutyPercent = s.dutyPercent;
            out.phase = s.closePhase;
            out.target = s.status.target;
            out.origin = s.origin;
            return out;
        }
    } // namespace

    void WebServer::publishTelemetry(const RoofIo::Snapshot &snapshot)
    {
        const MQTTConfig::Telemetry &cfg = getConfigCallback().mqtt.telemetry;
        telemetrySchedule.configure(cfg.enabled, cfg.restVoltsSeconds);
        if (!cfg.enabled)
        {
            telemetryMovePending = false;
            return;
        }
        const uint32_t now = millis();
        const Telemetry::Kind kind = telemetrySchedule.due(RoofIo::roofMoving(snapshot.status.state), snapshot.supplyVolts, now);
        // Not started with too little of the pass left (spec 032); the next check tries again.
        if (kind != Telemetry::Kind::None && mqttClient->mayWrite())
        {
            char payload[Telemetry::SAMPLE_BYTES];
            const Telemetry::Stamp stamp = Telemetry::stampOf(epochMsNow(), now, now);
            const size_t length = kind == Telemetry::Kind::Moving
                                      ? Telemetry::writeSample(payload, sizeof(payload), sampleOf(snapshot), stamp)
                                      : Telemetry::writeRest(payload, sizeof(payload), snapshot.supplyVolts, stamp);
            if (length > 0)
                mqttClient->publishSubtopicBytes(Telemetry::SUBTOPIC, payload, length, false);
            telemetrySchedule.published(kind, snapshot.supplyVolts, now); // sent or dropped, never retried
        }
        publishMoveRecord(snapshot, now);
    }

    // The move log exists only while diagnostics are on (spec 017).
    void WebServer::publishMoveRecord(const RoofIo::Snapshot &snapshot, uint32_t now)
    {
        if (!diagOn())
        {
            telemetryMovePending = false;
            return;
        }
        if (now - lastTelemetryMoveCheck >= TELEMETRY_MOVE_CHECK_MS)
        {
            lastTelemetryMoveCheck = now;
            if (telemetryMoveCue.newMove(roof->diagFinished(), mqttClient->connectionCount()))
            {
                telemetryMovePending = true;
                telemetryMoveConnection = mqttClient->connectionCount();
            }
        }
        if (!telemetryMovePending || !mqttClient->mayWrite())
            return;
        telemetryMovePending = false;
        // Waited through a reconnect: dropped, like every record while MQTT was down.
        if (mqttClient->connectionCount() != telemetryMoveConnection)
            return;
        Diag::Summary move;
        // A new move already under way: the finished one is dropped (it stays in /api/diag/moves).
        if (!roof->diagLatest(move) || move.active)
            return;
        char payload[Telemetry::MOVE_BYTES];
        const size_t length =
            Telemetry::writeMove(payload, sizeof(payload), move, Telemetry::stampOf(epochMsNow(), now, move.endMs), snapshot.currentSense);
        if (length > 0)
            mqttClient->publishSubtopicBytes(Telemetry::MOVE_SUBTOPIC, payload, length, false);
    }
} // namespace SQM
