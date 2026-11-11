#include "AlertEngine.h"

// The roof's non-critical events (RoofRoof spec 007): a fault, a low supply,
// a lost weather source, the automatic close starting and the roof closed
// after it. The critical ones (mount not parked, roof didn't close) don't come
// through here: the close sequence raises them past every rule (spec 005).

namespace SQM
{
    namespace Alerts
    {
        namespace
        {
            // The built-in wording, as templates: the web UI shows the same text
            // as the placeholder (web/src/components/settings/alertsShared.ts).
            struct Wording
            {
                AlertType type;
                const char *title;
                const char *message;
            };
            constexpr Wording FAULT = {
                AlertType::MoveFault,
                "Roof fault",
                "The roof stopped with a fault and won't move until a person clears it. Fault: {reason}"};
            constexpr Wording SUPPLY = {
                AlertType::SupplyLow, "12 V supply low", "The roof won't move until the supply is back above its minimum. {reason}"};
            constexpr Wording SOURCE = {
                AlertType::SafetyLost, "Weather source lost", "The weather counts as unsafe until it is back. {reason}"};
            constexpr Wording CLOSING = {
                AlertType::UnsafeClosing,
                "Unsafe: closing",
                "The weather isn't safe: the mount is being parked and the roof closed. Weather: {reason}"};
            constexpr Wording CLOSED = {AlertType::RoofClosed, "Roof closed after unsafe weather", "The roof is closed. Weather: {reason}"};

            Alert roofAlert(const Wording &wording, const std::string &reason)
            {
                Alert alert;
                alert.type = wording.type;
                alert.vars = {{"reason", reason}};
                alert.title = wording.title;
                alert.message = renderTemplate(wording.message, alert.vars);
                return alert;
            }

            enum RoofBit : uint8_t
            {
                BitFault = 1u << 0,
                BitSupply = 1u << 1,
                BitSource = 1u << 2,
                BitClosing = 1u << 3,
            };
        } // namespace

        bool AlertEngine::rise(Tracker &tracker, bool current, const Emit &emit, bool inGrace)
        {
            tracker.initialized = true;
            if (!current)
            {
                // Over: the next time it starts is a new event.
                tracker.notified = false;
                tracker.pending = false;
                return false;
            }
            if (tracker.notified)
                return false;
            if (!emit.allowed)
            {
                // The event is off: follow the condition silently, so switching
                // it on doesn't announce a condition that started before.
                tracker.notified = true;
                tracker.pending = false;
                return false;
            }
            if (!tracker.pending)
            {
                tracker.pending = true;
                tracker.pendingSince = emit.now;
            }
            if (inGrace || emit.now - tracker.pendingSince < emit.settle)
                return false;
            // Held by the cooldown: sent when it ends if the condition still holds.
            if (tracker.hasNotified && emit.now - tracker.lastNotifiedAt < emit.cooldown)
                return false;
            tracker.notified = true;
            tracker.pending = false;
            tracker.hasNotified = true;
            tracker.lastNotifiedAt = emit.now;
            return true;
        }

        void AlertEngine::updateRoof(const AlertInputs &in, const AlertRules &rules, std::vector<Alert> &alerts)
        {
            const RoofInputs &roof = in.roof;
            if (!roof.known)
                return;
            const uint32_t now = in.nowSeconds;
            const bool inGrace = now - startedAt < rules.startupGraceSeconds;
            const uint32_t cooldown = roofCooldown(rules.cooldownSeconds);
            const auto emit = [&](bool on, uint32_t settle) { return Emit{now, cooldown, on, settle}; };

            if (rise(roofFault, roof.faultLatched, emit(rules.onMoveFault, 0), inGrace))
                alerts.push_back(roofAlert(FAULT, roof.fault));
            if (rise(roofSupply, roof.supplyLow, emit(rules.onSupplyLow, ROOF_SETTLE_SECONDS), inGrace))
                alerts.push_back(roofAlert(SUPPLY, roof.supply));
            // A source still waiting for its first reply: not known yet, so
            // a loss announced before a reset stays announced.
            if (roof.sourceKnown && rise(roofSource, roof.sourceLost, emit(rules.onSafetyLost, ROOF_SETTLE_SECONDS), inGrace))
                alerts.push_back(roofAlert(SOURCE, roof.source));
            if (rise(roofClosing, roof.closing, emit(rules.onUnsafeClosing, 0), inGrace))
                alerts.push_back(roofAlert(CLOSING, roof.weather));

            // Each close an attempt saw finish, held through the grace and the
            // cooldown (a closed magnet that flickers can't send one each).
            if (roof.closedCount != closedSeen)
            {
                closedSeen = roof.closedCount;
                closedPending = rules.onRoofClosed;
                closedWeather = roof.weather;
            }
            if (!rules.onRoofClosed)
                closedPending = false;
            if (closedPending && !inGrace && (!closedSent || now - closedSentAt >= cooldown))
            {
                closedPending = false;
                closedSent = true;
                closedSentAt = now;
                alerts.push_back(roofAlert(CLOSED, closedWeather));
            }
        }

        uint8_t AlertEngine::announcedRoof() const
        {
            uint8_t bits = 0;
            if (roofFault.notified)
                bits |= BitFault;
            if (roofSupply.notified)
                bits |= BitSupply;
            if (roofSource.notified)
                bits |= BitSource;
            if (roofClosing.notified)
                bits |= BitClosing;
            return bits;
        }

        void AlertEngine::restoreAnnouncedRoof(uint8_t bits)
        {
            const auto restore = [](Tracker &tracker, bool announced)
            {
                tracker.initialized = true;
                tracker.notified = announced;
                tracker.pending = false;
            };
            restore(roofFault, (bits & BitFault) != 0);
            restore(roofSupply, (bits & BitSupply) != 0);
            restore(roofSource, (bits & BitSource) != 0);
            restore(roofClosing, (bits & BitClosing) != 0);
        }

        uint32_t announcedCheck(uint32_t bits)
        {
            return (bits ^ 0xA5A5F00Du) * 2654435761u;
        }

        bool announcedValid(uint32_t bits, uint32_t check)
        {
            return bits <= 0x0Fu && check == announcedCheck(bits);
        }

        void AlertEngine::addRoofWaits(std::vector<Wait> &out, uint32_t now, const AlertRules &rules) const
        {
            const uint32_t cooldown = roofCooldown(rules.cooldownSeconds);
            const auto add = [&](const char *name, const Tracker &t, uint32_t settle)
            {
                if (!t.pending)
                    return;
                const uint32_t held = now - t.pendingSince;
                if (held < settle)
                    out.push_back({std::string("roof:") + name, WaitKind::Settle, settle - held});
                else if (t.hasNotified && now - t.lastNotifiedAt < cooldown)
                    out.push_back({std::string("roof:") + name, WaitKind::Cooldown, cooldown - (now - t.lastNotifiedAt)});
            };
            add(alertTypeName(AlertType::MoveFault), roofFault, 0);
            add(alertTypeName(AlertType::SupplyLow), roofSupply, ROOF_SETTLE_SECONDS);
            add(alertTypeName(AlertType::SafetyLost), roofSource, ROOF_SETTLE_SECONDS);
            add(alertTypeName(AlertType::UnsafeClosing), roofClosing, 0);
        }
    } // namespace Alerts
} // namespace SQM
