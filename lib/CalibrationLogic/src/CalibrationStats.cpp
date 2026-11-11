#include "CalibrationStats.h"

#include <algorithm>
#include <cmath>

namespace SQM
{
    namespace Calib
    {
        void Stats::add(uint16_t counts)
        {
            ++count;
            sum += counts;
            sumSq += static_cast<double>(counts) * counts;
            min = std::min(min, counts);
            max = std::max(max, counts);
        }

        void Stats::merge(const Stats &other)
        {
            if (other.empty())
                return;
            count += other.count;
            sum += other.sum;
            sumSq += other.sumSq;
            min = std::min(min, other.min);
            max = std::max(max, other.max);
        }

        double Stats::mean() const
        {
            return count == 0 ? 0.0 : sum / count;
        }

        double Stats::sigma() const
        {
            if (count < 2)
                return 0.0;
            const double m = mean();
            const double variance = sumSq / count - m * m;
            return variance > 0.0 ? std::sqrt(variance) : 0.0;
        }

        void LegSamples::clear()
        {
            for (Stats &bucket : buckets)
                bucket = Stats();
            width = FIRST_WIDTH_MS;
        }

        void LegSamples::widen()
        {
            for (uint8_t i = 0; i < BUCKETS / 2; ++i)
            {
                Stats merged = buckets[2 * i];
                merged.merge(buckets[2 * i + 1]);
                buckets[i] = merged;
            }
            for (uint8_t i = BUCKETS / 2; i < BUCKETS; ++i)
                buckets[i] = Stats();
            width *= 2;
        }

        void LegSamples::add(uint32_t drivenMs, uint16_t counts)
        {
            // A leg never exceeds MAX_CALIBRATION_LEG_MS, so this ends within a few doublings.
            while (drivenMs / width >= BUCKETS)
                widen();
            buckets[drivenMs / width].add(counts);
        }

        Stats LegSamples::middle(uint32_t totalMs) const
        {
            Stats out;
            const uint64_t from = totalMs / 4;
            const uint64_t to = static_cast<uint64_t>(totalMs) * 3 / 4;
            for (uint32_t i = 0; i < BUCKETS; ++i)
            {
                const uint64_t start = static_cast<uint64_t>(i) * width;
                if (start >= from && start + width <= to)
                    out.merge(buckets[i]);
            }
            if (out.empty())
            {
                const uint32_t mid = std::min<uint32_t>((totalMs / 2) / width, BUCKETS - 1);
                out.merge(buckets[mid]);
            }
            return out;
        }

        void Electrical::addSupply(float volts)
        {
            if (!std::isfinite(volts))
                return;
            supplyMin = supplyRead ? std::min(supplyMin, volts) : volts;
            supplyRead = true;
        }

        void Electrical::addAmps(float amps)
        {
            if (!std::isfinite(amps))
                return;
            ++count;
            ampsSum += amps;
            ampsPeak = std::max(ampsPeak, amps);
        }

        float Electrical::ampsMean() const
        {
            return count == 0 ? 0.0f : static_cast<float>(ampsSum / count);
        }
    } // namespace Calib
} // namespace SQM
