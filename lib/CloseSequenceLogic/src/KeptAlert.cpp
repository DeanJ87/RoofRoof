#include "KeptAlert.h"

namespace SQM
{
    namespace CloseSeq
    {
        namespace
        {
            constexpr size_t HEADER_BYTES = 8; // version, type, level, variables, queuedAt
            constexpr size_t CHECK_BYTES = 4;

            uint32_t checksum(const uint8_t *data, size_t length)
            {
                uint32_t sum = 2166136261u; // FNV-1a
                for (size_t i = 0; i < length; ++i)
                    sum = (sum ^ data[i]) * 16777619u;
                return sum;
            }

            void putU32(std::vector<uint8_t> &out, uint32_t value)
            {
                for (int i = 0; i < 4; ++i)
                    out.push_back(static_cast<uint8_t>(value >> (8 * i)));
            }

            uint32_t getU32(const uint8_t *data)
            {
                return static_cast<uint32_t>(data[0]) | static_cast<uint32_t>(data[1]) << 8 | static_cast<uint32_t>(data[2]) << 16 |
                       static_cast<uint32_t>(data[3]) << 24;
            }

            void putText(std::vector<uint8_t> &out, const std::string &text)
            {
                const size_t length = text.size() < KEPT_ALERT_MAX_TEXT ? text.size() : KEPT_ALERT_MAX_TEXT;
                out.push_back(static_cast<uint8_t>(length));
                out.push_back(static_cast<uint8_t>(length >> 8));
                out.insert(out.end(), text.begin(), text.begin() + static_cast<std::ptrdiff_t>(length));
            }

            // Reads one text at `at` within `end`; false when it runs past it.
            bool getText(const uint8_t *data, size_t end, size_t &at, std::string &out)
            {
                if (at + 2 > end)
                    return false;
                const size_t length = static_cast<size_t>(data[at]) | static_cast<size_t>(data[at + 1]) << 8;
                at += 2;
                if (length > KEPT_ALERT_MAX_TEXT || at + length > end)
                    return false;
                out.assign(reinterpret_cast<const char *>(data + at), length);
                at += length;
                return true;
            }
        } // namespace

        std::vector<uint8_t> encodeKept(const KeptAlert &kept)
        {
            const Alerts::Alert &alert = kept.alert;
            const size_t vars = alert.vars.size() < KEPT_ALERT_MAX_VARS ? alert.vars.size() : KEPT_ALERT_MAX_VARS;
            std::vector<uint8_t> out;
            out.reserve(64 + alert.title.size() + alert.message.size());
            out.push_back(KEPT_ALERT_VERSION);
            out.push_back(static_cast<uint8_t>(alert.type));
            out.push_back(static_cast<uint8_t>(alert.level));
            out.push_back(static_cast<uint8_t>(vars));
            putU32(out, kept.queuedAt);
            putText(out, alert.title);
            putText(out, alert.message);
            for (size_t i = 0; i < vars; ++i)
            {
                putText(out, alert.vars[i].first);
                putText(out, alert.vars[i].second);
            }
            if (out.size() + CHECK_BYTES > KEPT_ALERT_MAX_BYTES)
            {
                // Too long with its variables: the texts alone still say it all.
                KeptAlert shorter = kept;
                shorter.alert.vars.clear();
                return encodeKept(shorter);
            }
            putU32(out, checksum(out.data(), out.size()));
            return out;
        }

        bool decodeKept(const uint8_t *data, size_t length, KeptAlert &out)
        {
            if (data == nullptr || length < HEADER_BYTES + CHECK_BYTES || length > KEPT_ALERT_MAX_BYTES)
                return false;
            const size_t end = length - CHECK_BYTES;
            if (data[0] != KEPT_ALERT_VERSION || getU32(data + end) != checksum(data, end))
                return false;
            if (data[1] >= Alerts::ALERT_TYPE_COUNT || data[2] > static_cast<uint8_t>(Alerts::AlertLevel::Wake) ||
                data[3] > KEPT_ALERT_MAX_VARS)
                return false;
            KeptAlert kept;
            kept.alert.type = static_cast<Alerts::AlertType>(data[1]);
            kept.alert.level = static_cast<Alerts::AlertLevel>(data[2]);
            kept.queuedAt = getU32(data + 4);
            size_t at = HEADER_BYTES;
            if (!getText(data, end, at, kept.alert.title) || !getText(data, end, at, kept.alert.message))
                return false;
            for (uint8_t i = 0; i < data[3]; ++i)
            {
                std::pair<std::string, std::string> var;
                if (!getText(data, end, at, var.first) || !getText(data, end, at, var.second))
                    return false;
                kept.alert.vars.push_back(var);
            }
            if (at != end)
                return false;
            out = kept;
            return true;
        }

        Alerts::Alert markRestarted(Alerts::Alert alert, const std::string &when)
        {
            const std::string note = when.empty() ? "Sent before a restart." : "Sent before a restart at " + when + ".";
            alert.message += "\n\n" + note;
            for (auto &var : alert.vars)
                if (var.first == "details")
                    var.second += "\n\n" + note;
            return alert;
        }
    } // namespace CloseSeq
} // namespace SQM
