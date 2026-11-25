#include "AlertOutbox.h"

#include <Arduino.h>
#include <Preferences.h>
#include <ctime>
#include <esp_heap_caps.h>

#include "DeviceCore.h"
#include "HeapGuard.h"
#include "KeptAlert.h"
#include "Logger.h"
#include "PlatformGuard.h"

namespace SQM
{
    namespace
    {
        constexpr const char *NVS_NAMESPACE = "alertq";

        // "k5": the record of the event with that AlertType number.
        String keyOf(Alerts::AlertType type)
        {
            return String("k") + String(static_cast<unsigned>(type));
        }

        uint32_t epochNow()
        {
            const time_t now = time(nullptr);
            return now >= Core::CLOCK_VALID_EPOCH ? static_cast<uint32_t>(now) : 0;
        }

        std::string localTime(uint32_t epoch)
        {
            if (epoch == 0)
                return "";
            const time_t when = static_cast<time_t>(epoch);
            struct tm local;
            localtime_r(&when, &local);
            char text[24];
            strftime(text, sizeof(text), "%Y-%m-%d %H:%M", &local);
            return text;
        }
    } // namespace

    const Alerts::AlertType AlertOutbox::KEPT[KEPT_TYPES] = {
        Alerts::AlertType::MountNotParked, Alerts::AlertType::RoofNotClosed, Alerts::AlertType::NeedsPerson};

    // Reading only: no flash write here. A record that doesn't check out is
    // dropped (and erased with the next write), never sent half-read.
    void AlertOutbox::begin()
    {
        Preferences prefs;
        if (!prefs.begin(NVS_NAMESPACE, true))
            return; // nothing kept yet
        for (size_t i = 0; i < KEPT_TYPES; ++i)
        {
            const String key = keyOf(KEPT[i]);
            const size_t length = prefs.isKey(key.c_str()) ? prefs.getBytesLength(key.c_str()) : 0;
            if (length == 0)
                continue;
            std::vector<uint8_t> bytes(length);
            prefs.getBytes(key.c_str(), bytes.data(), length);
            CloseSeq::KeptAlert kept;
            if (!CloseSeq::decodeKept(bytes.data(), length, kept) || kept.alert.type != KEPT[i])
            {
                Logger::warn(TAG, "Kept alert %s unreadable: dropped", key.c_str());
                keptSerial[i] = UINT32_MAX; // erased by keep()
                continue;
            }
            Logger::warn(TAG, "Alert \"%s\" waited for a channel before the restart: sending it again", kept.alert.title.c_str());
            // Not resolvable: it happened, whatever the roof does now.
            keptSerial[i] = queue.push(CloseSeq::markRestarted(kept.alert, localTime(kept.queuedAt)), false);
            queuedAt[i] = kept.queuedAt;
        }
        prefs.end();
    }

    void AlertOutbox::push(const Alerts::Alert &alert, bool resolvable)
    {
        queue.push(alert, resolvable);
        for (size_t i = 0; i < KEPT_TYPES; ++i)
            if (KEPT[i] == alert.type)
                queuedAt[i] = epochNow();
    }

    void AlertOutbox::handle(bool networkUp, const CloseSeq::AlertQueue::Send &send, const CloseSeq::AlertQueue::Probe &probe)
    {
        const uint32_t now = millis();
        if (stepped && now - lastStepMs < STEP_EVERY_MS)
            return;
        stepped = true;
        lastStepMs = now;
        // Spec 035: a send on a heap too short for it would abort the device
        // (no exceptions); the alert waits for the next step instead.
        const uint32_t largest = static_cast<uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        const bool fits = HeapSafe::alertFits(largest, ESP.getFreeHeap());
        if (fits != memoryFits)
        {
            memoryFits = fits;
            if (!fits && queue.size() > 0)
                Logger::warn(TAG, "Low memory (largest block %lu B): alerts wait", static_cast<unsigned long>(largest));
        }
        if (!fits)
            return;
        queue.step(now, networkUp, send, probe);
        keep();
    }

    // One NVS write per change of a critical event's waiting alert (queued,
    // superseded, gone), on the main loop only, never from an interrupt or a
    // timer callback, and never while the roof moves or a close waits (spec
    // 010's flash-write hold): then it waits for a later pass. Spec 029 looks
    // into flash writes and interrupts; keep these rare.
    void AlertOutbox::keep()
    {
        for (size_t i = 0; i < KEPT_TYPES; ++i)
        {
            const CloseSeq::QueuedAlert *waiting = queue.find(KEPT[i]);
            const uint32_t want = waiting != nullptr ? waiting->serial : 0;
            if (want == keptSerial[i])
                continue;
            const PlatformGuard::FlashWrite write;
            if (write.refused() != RoofIo::HoldReason::None)
                return;
            Preferences prefs;
            if (!prefs.begin(NVS_NAMESPACE, false))
                return;
            const String key = keyOf(KEPT[i]);
            bool ok = true;
            if (waiting != nullptr)
            {
                const std::vector<uint8_t> bytes = CloseSeq::encodeKept({waiting->alert, queuedAt[i]});
                ok = prefs.putBytes(key.c_str(), bytes.data(), bytes.size()) == bytes.size();
            }
            else if (prefs.isKey(key.c_str()))
                ok = prefs.remove(key.c_str());
            prefs.end();
            if (!ok)
                Logger::warn(TAG, "Couldn't keep alert %s in NVS", key.c_str());
            keptSerial[i] = want; // not tried again on every pass if the write failed
        }
    }
} // namespace SQM
