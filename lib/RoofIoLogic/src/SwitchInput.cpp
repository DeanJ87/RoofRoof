#include "SwitchInput.h"

namespace SQM
{
    namespace RoofIo
    {
        Contact SwitchInput::update(bool levelLow, uint32_t nowMs)
        {
            if (started && levelLow == lastLow)
            {
                if (run < INPUT_CONFIRM_READINGS)
                    ++run;
            }
            else
            {
                run = 1;
            }
            started = true;
            lastLow = levelLow;

            if (run >= INPUT_CONFIRM_READINGS)
            {
                confirmed = true;
                confirmedLow = levelLow;
                lastConfirmMs = nowMs;
            }

            // Unsigned difference: right across the 32-bit wrap.
            const bool chattering = confirmed && nowMs - lastConfirmMs > INPUT_CHATTER_MS;
            if (!confirmed || chattering)
                current = Contact::Unreadable;
            else
                current = confirmedLow ? Contact::Closed : Contact::Open;
            return current;
        }

        Safety::SwitchReading parkReading(Contact contact)
        {
            switch (contact)
            {
            case Contact::Closed:
                return Safety::SwitchReading::Closed;
            case Contact::Open:
                return Safety::SwitchReading::Open;
            default:
                return Safety::SwitchReading::Unreadable;
            }
        }

        Safety::RainContact rainReading(Contact contact)
        {
            switch (contact)
            {
            case Contact::Closed:
                return Safety::RainContact::Raining;
            case Contact::Open:
                return Safety::RainContact::Dry;
            default:
                return Safety::RainContact::Unreadable;
            }
        }

        const char *contactName(Contact contact)
        {
            switch (contact)
            {
            case Contact::Closed:
                return "closed";
            case Contact::Open:
                return "open";
            default:
                return "unreadable";
            }
        }
    } // namespace RoofIo
} // namespace SQM
