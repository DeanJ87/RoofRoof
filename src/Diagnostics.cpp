#include "Diagnostics.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <ctime>
#include <new>
#include <vector>

#include "DeviceCore.h"
#include "Logger.h"
#include "PlatformGuard.h"
#include "RoofControl.h"

// Spec 017 (research R2, R8, R11): the log's memory, the summary list, the
// loop pass timer and the kept file. Main loop, except the copies.

namespace SQM
{
    namespace
    {
        Diag::Clock clockNow()
        {
            const time_t epoch = time(nullptr);
            Diag::Clock clock;
            clock.nowMs = millis();
            clock.epoch = static_cast<int64_t>(epoch) >= Core::CLOCK_VALID_EPOCH ? static_cast<uint32_t>(epoch) : 0;
            return clock;
        }
    } // namespace

    void Diagnostics::begin(const Config &config, RoofControl *control)
    {
        roof = control;
        updateConfig(config.diag);
        applyConfig();
    }

    void Diagnostics::updateConfig(const DiagConfig &config)
    {
        wantEnabled.store(config.enabled);
        keepNow.store(config.enabled && config.keepSummaries);
        mqttNow.store(config.enabled && config.mqtt);
    }

    void Diagnostics::applyConfig()
    {
        const bool want = wantEnabled.load();
        if (want && !enabledNow.load())
            enable();
        else if (!want && enabledNow.load())
            disable();
    }

    // The log and the list from the heap, in one piece each (FR-025); without
    // the memory the documents say the log is unavailable and nothing else changes.
    void Diagnostics::enable()
    {
        log.reset(new (std::nothrow) Diag::MoveLog());
        scratch.reset(new (std::nothrow) Diag::Move());
        std::unique_ptr<Diag::SummaryList> fresh(new (std::nothrow) Diag::SummaryList());
        enabledNow.store(true);
        if (!log || !scratch || !fresh || roof == nullptr)
        {
            log.reset();
            scratch.reset();
            logReadyNow.store(false);
            Logger::error(TAG, "No memory for the diagnostics log; diagnostics show live values only");
            return;
        }
        {
            std::lock_guard<std::mutex> guard(listLock);
            list = std::move(fresh);
        }
        lastAddedId = 0;
        maxPassUsNow.store(0);
        if (keepNow.load())
            loadKept();
        keptWrite.start(summariesRevision());
        roof->attachDiag(log.get());
        logReadyNow.store(true);
        Logger::info(TAG, "Advanced diagnostics on");
    }

    void Diagnostics::disable()
    {
        enabledNow.store(false);
        logReadyNow.store(false);
        timing.store(false);
        // Taken back from the control step under the roof's lock before it is freed.
        Diag::MoveLog *attached = roof != nullptr ? roof->detachDiag() : nullptr;
        if (attached != log.get())
            Logger::error(TAG, "Diagnostics log mismatch on detach");
        log.reset();
        scratch.reset();
        std::lock_guard<std::mutex> guard(listLock);
        list.reset();
        Logger::info(TAG, "Advanced diagnostics off");
    }

    void Diagnostics::handle()
    {
        applyConfig();
        if (!log)
            return;
        const uint32_t now = millis();
        if (clearPending.exchange(false))
            keptWrite.cleared();
        if (now - lastCollectMs >= COLLECT_EVERY_MS)
        {
            lastCollectMs = now;
            collectFinished();
        }
        if (keptWrite.due(now, summariesRevision(), keepNow.load()))
            writeKept(now);
    }

    // Finished moves into the list, oldest first, once each.
    void Diagnostics::collectFinished()
    {
        uint32_t ids[Diag::MAX_MOVES];
        const uint8_t count = roof->diagIds(ids);
        for (int i = count - 1; i >= 0; --i)
        {
            if (ids[i] <= lastAddedId || !roof->diagMove(ids[i], *scratch) || scratch->summary.active)
                continue;
            lastAddedId = ids[i];
            std::lock_guard<std::mutex> guard(listLock);
            if (list)
                list->add(scratch->summary);
        }
    }

    void Diagnostics::loadKept()
    {
        File file = LittleFS.open(Diag::KEPT_PATH, "r");
        if (!file)
            return;
        std::vector<uint8_t> bytes(Diag::KEPT_MAX_BYTES + 1);
        const size_t size = file.read(bytes.data(), bytes.size());
        file.close();
        uint32_t nextId = 1;
        bool loaded = false;
        {
            std::lock_guard<std::mutex> guard(listLock);
            loaded = list && Diag::decodeKept(bytes.data(), size, *list, nextId);
            if (loaded)
                lastAddedId = list->highestId();
        }
        if (!loaded)
        {
            Logger::warn(TAG, "Kept move summaries unreadable; replaced at the next write");
            return;
        }
        log->setNextId(nextId);
        Logger::info(TAG, "Kept move summaries loaded");
    }

    // Through spec 010's flash-write guard: never while the roof moves, a
    // close waits, the close sequence is busy or a restart is committed.
    void Diagnostics::writeKept(uint32_t nowMs)
    {
        PlatformGuard::FlashWrite write;
        if (write.refused() != RoofIo::HoldReason::None)
            return; // tried again on a later pass
        std::vector<uint8_t> bytes(Diag::KEPT_MAX_BYTES);
        size_t size = 0;
        uint32_t revision = 0;
        {
            std::lock_guard<std::mutex> guard(listLock);
            if (!list)
                return;
            revision = list->revision();
            size = Diag::encodeKept(*list, log->nextId(), clockNow(), bytes.data(), bytes.size());
        }
        File file = LittleFS.open(Diag::KEPT_PATH, "w");
        const bool ok = file && file.write(bytes.data(), size) == size;
        if (file)
            file.close();
        keptWrite.written(nowMs, revision); // a failure is tried again after the interval
        if (!ok)
            Logger::warn(TAG, "Couldn't write the kept move summaries");
    }

    void Diagnostics::passStarted()
    {
        const bool on = enabledNow.load();
        timing.store(on);
        if (on)
            passStartUs = micros();
    }

    void Diagnostics::passEnded()
    {
        if (!timing.load())
            return;
        const uint32_t us = micros() - passStartUs;
        passUsNow.store(us);
        if (us > maxPassUsNow.load())
            maxPassUsNow.store(us);
    }

    void Diagnostics::copySummaries(Diag::SummaryList &out) const
    {
        std::lock_guard<std::mutex> guard(listLock);
        if (list)
            out = *list;
        else
            out.clear();
    }

    uint32_t Diagnostics::summariesRevision() const
    {
        std::lock_guard<std::mutex> guard(listLock);
        return list ? list->revision() : 0;
    }

    void Diagnostics::clearSummaries()
    {
        {
            std::lock_guard<std::mutex> guard(listLock);
            if (list)
                list->clear();
        }
        clearPending.store(true);
    }
} // namespace SQM
