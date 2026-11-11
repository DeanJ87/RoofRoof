#include "DiagSchedule.h"

namespace SQM
{
    namespace Diag
    {
        uint32_t streamIntervalMs(Roof::State state)
        {
            return moving(state) ? STREAM_MOVING_MS : STREAM_REST_MS;
        }

        bool MqttSchedule::liveDue(uint32_t nowMs, bool busy) const
        {
            return !busy && (!liveAny || nowMs - lastLiveMs >= MQTT_LIVE_MS);
        }

        void MqttSchedule::livePublished(uint32_t nowMs)
        {
            liveAny = true;
            lastLiveMs = nowMs;
        }

        int MqttSchedule::nextSummary(const SummaryList &list, bool busy) const
        {
            if (busy)
                return -1;
            for (int i = static_cast<int>(list.size()) - 1; i >= 0; --i)
            {
                const Summary &s = list.at(static_cast<uint8_t>(i));
                if (!s.kept && !s.active && s.id > publishedId)
                    return i;
            }
            return -1;
        }
    } // namespace Diag
} // namespace SQM
