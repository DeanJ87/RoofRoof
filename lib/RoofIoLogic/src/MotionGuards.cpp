#include "MotionGuards.h"

#include <cmath>
#include <cstring>
#include <limits>

#include "PlatformHold.h"
#include "StepWatchdog.h"

namespace SQM
{
    namespace RoofIo
    {
        bool roofMoving(Roof::State state)
        {
            return state == Roof::State::Opening || state == Roof::State::Closing;
        }

        bool platformActionRefused(Roof::State state)
        {
            return roofMoving(state);
        }

        bool commandLineOtaAllowed(Roof::State state)
        {
            return state == Roof::State::Closed;
        }

        uint32_t FlashActivity::begin(uint32_t nowMs)
        {
            current = nextToken++;
            if (nextToken == 0)
                nextToken = 1;
            active = true;
            stopRequested = false;
            lastProgressMs = nowMs;
            return current;
        }

        void FlashActivity::progress(uint32_t token, uint32_t nowMs)
        {
            if (token == current && active)
                lastProgressMs = nowMs;
        }

        void FlashActivity::end(uint32_t token)
        {
            if (token == current)
                active = false;
        }

        bool FlashActivity::busy(uint32_t nowMs) const
        {
            return active && stepGapMs(nowMs, lastProgressMs) <= FLASH_STALE_MS;
        }

        void FlashActivity::requestStop()
        {
            if (active)
                stopRequested = true;
        }

        bool FlashActivity::shouldStop(uint32_t token) const
        {
            return token != current || !active || stopRequested;
        }

        void FlashActivity::holdShort()
        {
            ++shortWrites;
        }

        void FlashActivity::releaseShort()
        {
            if (shortWrites > 0)
                --shortWrites;
        }

        void RestartPlan::schedule(bool isMandatory)
        {
            if (!due)
                mustRun = false;
            due = true;
            mustRun = mustRun || isMandatory;
        }

        bool RestartPlan::cancel()
        {
            if (!due || mustRun || isCommitted)
                return false;
            due = false;
            return true;
        }

        void RestartPlan::clear()
        {
            if (isCommitted)
                return;
            due = false;
            mustRun = false;
            isCommitted = false;
        }

        bool RestartPlan::commit()
        {
            if (!due)
                return false;
            isCommitted = true;
            return true;
        }

        BlockerDecision blockerDecision(Roof::Command command, const Blockers &b)
        {
            BlockerDecision d;
            // A WiFi change or scan is waited for like a flash write: a close
            // started under it could lose N.I.N.A. part-way (spec 010).
            const bool flash = b.flashBusy || b.flashWriting || b.networkChanging;
            if (command == Roof::Command::Open)
            {
                if (flash || b.mqttConnecting || b.restartScheduled || b.restartCommitted || b.hotspotOn)
                    d.action = BlockerAction::Refuse;
            }
            else if (command == Roof::Command::Close)
            {
                if (b.restartCommitted)
                {
                    d.action = BlockerAction::Refuse;
                    return d;
                }
                d.cancelRestart = b.restartScheduled && b.restartCancellable;
                d.stopFlash = b.flashBusy;
                if (flash || b.mqttConnecting)
                    d.action = BlockerAction::DeferClose;
            }
            return d;
        }

        const char *blockerText(const Blockers &b)
        {
            if (b.restartCommitted)
                return "The device is restarting";
            if (b.hotspotOn)
                return HOTSPOT_TEXT;
            if (b.flashBusy)
                return "An upload is being written";
            if (b.flashWriting)
                return "A settings save is being written";
            if (b.networkChanging)
                return "The WiFi network is changing";
            if (b.mqttConnecting)
                return "MQTT is connecting";
            if (b.restartScheduled)
                return "A restart is scheduled";
            return "";
        }

        const char *blockerReason(const Blockers &b)
        {
            if (b.restartCommitted)
                return RESTARTING_REASON;
            return b.hotspotOn ? HOTSPOT_REASON : "busy";
        }

        bool commandHeaderValid(const char *value)
        {
            return value != nullptr && std::strcmp(value, "1") == 0;
        }

        bool watchdogAlive(uint32_t nowMs, uint32_t beatMs, uint32_t limitMs)
        {
            return stepGapMs(nowMs, beatMs) <= limitMs;
        }

        bool watchdogStoppedFault(bool outputOn, uint32_t nowMs, uint32_t beatMs)
        {
            return outputOn && !watchdogAlive(nowMs, beatMs);
        }

        float runningAmps(float readAmps, const Roof::Status &status, const Roof::Output &applied, float minRunningAmps)
        {
            if (!std::isfinite(readAmps))
                return readAmps;
            const bool on = applied.command != Driver::Command::Off && applied.duty > 0;
            if (!on)
                return readAmps; // motor off: the reading is the sense output's offset
            const float amps = readAmps * 255.0f / static_cast<float>(applied.duty);
            const bool settled = status.driving && status.moveElapsedMs >= CURRENT_SETTLE_MS;
            if (minRunningAmps > 0.0f && settled && amps < minRunningAmps)
                return std::numeric_limits<float>::quiet_NaN();
            return amps;
        }
    } // namespace RoofIo
} // namespace SQM
