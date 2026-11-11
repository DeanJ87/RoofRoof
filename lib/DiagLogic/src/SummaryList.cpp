#include "SummaryList.h"

#include <algorithm>

namespace SQM
{
    namespace Diag
    {
        void SummaryList::add(const Summary &summary)
        {
            const uint8_t kept = count < MAX_SUMMARIES ? count : static_cast<uint8_t>(MAX_SUMMARIES - 1);
            for (uint8_t i = kept; i > 0; --i)
                items[i] = items[i - 1];
            items[0] = summary;
            count = static_cast<uint8_t>(kept + 1);
            ++changes;
        }

        void SummaryList::load(const Summary *summaries, uint8_t loaded)
        {
            count = std::min<uint8_t>(loaded, MAX_SUMMARIES);
            for (uint8_t i = 0; i < count; ++i)
                items[i] = summaries[i];
            ++changes;
        }

        void SummaryList::clear()
        {
            count = 0;
            ++changes;
        }

        uint32_t SummaryList::highestId() const
        {
            uint32_t highest = 0;
            for (uint8_t i = 0; i < count; ++i)
                highest = std::max(highest, items[i].id);
            return highest;
        }
    } // namespace Diag
} // namespace SQM
