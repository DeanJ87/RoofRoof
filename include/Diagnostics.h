#pragma once

// Spec 017, advanced diagnostics on the main loop (research R2, R8, R11): owns
// the move log's memory (allocated only while diag.enabled is on, attached to
// the roof's control step under the roof's lock), the summary list of
// finished moves, the loop pass timer and the kept-summaries file, written
// only through PlatformGuard::FlashWrite. Nothing here feeds a decision.

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

#include "Config.h"
#include "KeptSummaries.h"
#include "MoveLog.h"
#include "SummaryList.h"

namespace SQM
{
    class RoofControl;

    class Diagnostics
    {
    public:
        Diagnostics() = default;
        Diagnostics(const Diagnostics &) = delete;
        Diagnostics &operator=(const Diagnostics &) = delete;

        // setup(), after the filesystem is mounted and the roof has started.
        void begin(const Config &config, RoofControl *roof);
        // Any task (a settings save): handed over, applied by handle().
        void updateConfig(const DiagConfig &config);
        // Main loop, every pass.
        void handle();
        // Main loop: around each pass's work (timed only while on).
        void passStarted();
        void passEnded();

        // Any task.
        bool enabled() const { return enabledNow.load(); }
        bool logReady() const { return logReadyNow.load(); }
        bool keepSummaries() const { return keepNow.load(); }
        bool mqttOn() const { return mqttNow.load(); }
        int64_t passMs() const { return timing.load() ? static_cast<int64_t>(passUsNow.load() / 1000) : -1; }
        int64_t maxPassMs() const { return timing.load() ? static_cast<int64_t>(maxPassUsNow.load() / 1000) : -1; }
        // A bounded copy of the summary list (empty while off).
        void copySummaries(Diag::SummaryList &out) const;
        uint32_t summariesRevision() const;
        // After RoofControl::diagClear() succeeded: the list empties and the
        // kept file is rewritten empty at the next chance.
        void clearSummaries();

    private:
        static constexpr const char *TAG = "Diag";
        static constexpr uint32_t COLLECT_EVERY_MS = 250;

        void applyConfig();
        void enable();
        void disable();
        void collectFinished();
        void loadKept();
        void writeKept(uint32_t nowMs);

        RoofControl *roof = nullptr;
        std::atomic<bool> wantEnabled{false};
        std::atomic<bool> enabledNow{false};
        std::atomic<bool> logReadyNow{false};
        std::atomic<bool> keepNow{false};
        std::atomic<bool> mqttNow{false};
        std::atomic<bool> clearPending{false};

        // Main loop only (the log itself only through the roof's lock once attached).
        std::unique_ptr<Diag::MoveLog> log;
        std::unique_ptr<Diag::Move> scratch;
        uint32_t lastAddedId = 0;
        uint32_t lastCollectMs = 0;
        Diag::KeptWrite keptWrite;

        // The list: the main loop writes it, any task copies it.
        std::unique_ptr<Diag::SummaryList> list;
        mutable std::mutex listLock;

        // The loop pass timer.
        std::atomic<bool> timing{false};
        uint32_t passStartUs = 0;
        std::atomic<uint32_t> passUsNow{0};
        std::atomic<uint32_t> maxPassUsNow{0};
    };
} // namespace SQM
