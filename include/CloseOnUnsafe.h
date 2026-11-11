#pragma once

// The automatic close on unsafe weather on the board (spec 005): gathers the
// weather verdict and park result (spec 004) and the roof's snapshot (spec
// 003) on the main loop, steps lib/CloseSequenceLogic's sequence, carries out
// its decision (a park request, or a close or stop with the automatic origin,
// which passes spec 003's gates like every other command), logs every change
// and hands the critical events to the alert sink. Every decision is the
// library's (constitution V); nothing here can open the roof.

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

#include "AlertEngine.h"
#include "CloseSequence.h"
#include "Config.h"

namespace SQM
{
    class RoofControl;
    class SafetyInputs;

    class CloseOnUnsafe
    {
    public:
        using AlertSink = std::function<void(const Alerts::Alert &)>;

        // setup(), before the main loop runs.
        void begin(const Config &config) { updateConfig(config); }
        // Any task (a settings save runs on the web server's task).
        void updateConfig(const Config &config) { parkTimeoutMs.store(config.park.timeoutSeconds * 1000u); }
        // Main loop, before handle() first runs.
        void setAlertSink(AlertSink sink) { alertSink = std::move(sink); }
        // Main loop, every pass, after the safety inputs were handled.
        void handle(SafetyInputs &safety, RoofControl &roof);

    private:
        static constexpr const char *TAG = "Close";

        CloseSeq::Decision carryOut(const CloseSeq::Decision &decision, SafetyInputs &safety, RoofControl &roof, std::string &refusal);
        void report(const CloseSeq::Decision &decision, const CloseSeq::Reasons &reasons, const std::string &weather);

        CloseSeq::Sequence sequence;
        std::atomic<uint32_t> parkTimeoutMs{CloseSeq::DEFAULT_PARK_TIMEOUT_MS};
        AlertSink alertSink;
        CloseSeq::Phase loggedPhase = CloseSeq::Phase::Idle;
    };
} // namespace SQM
