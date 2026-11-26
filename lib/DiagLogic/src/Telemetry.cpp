#include "Telemetry.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace SQM
{
    namespace Telemetry
    {
        namespace
        {
            // Appends to a fixed buffer; once anything doesn't fit, the whole
            // document is refused (length 0).
            class Writer
            {
            public:
                Writer(char *out, size_t size)
                    : buf(out),
                      cap(size)
                {
                    if (cap > 0)
                        buf[0] = '\0';
                }

                void text(const char *format, ...) __attribute__((format(printf, 2, 3)))
                {
                    if (full)
                        return;
                    va_list args;
                    va_start(args, format);
                    const int n = std::vsnprintf(buf + used, cap - used, format, args);
                    va_end(args);
                    if (n < 0 || static_cast<size_t>(n) >= cap - used)
                        full = true;
                    else
                        used += static_cast<size_t>(n);
                }

                // A whole number of up to 20 digits (no %llu: not every printf has it).
                void number(uint64_t value)
                {
                    char digits[21];
                    size_t n = 0;
                    do
                    {
                        digits[n++] = static_cast<char>('0' + value % 10);
                        value /= 10;
                    } while (value != 0 && n < sizeof(digits));
                    char forward[21];
                    for (size_t i = 0; i < n; ++i)
                        forward[i] = digits[n - 1 - i];
                    forward[n] = '\0';
                    text("%s", forward);
                }

                // ,"key":12.34 or ,"key":null
                void reading(const char *key, float value, int decimals)
                {
                    if (std::isfinite(value))
                        text(",\"%s\":%.*f", key, decimals, static_cast<double>(value));
                    else
                        text(",\"%s\":null", key);
                }

                // ,"key":"name" for a machine name (letters, digits, _ only;
                // anything else is written as "?", never unescaped).
                void name(const char *key, const char *value) { text(",\"%s\":\"%s\"", key, safeName(value)); }

                size_t finish()
                {
                    text("}");
                    return full ? 0 : used;
                }

            private:
                static const char *safeName(const char *value)
                {
                    if (value == nullptr || value[0] == '\0')
                        return "?";
                    for (const char *p = value; *p != '\0'; ++p)
                    {
                        const char c = *p;
                        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
                        if (!ok)
                            return "?";
                    }
                    return value;
                }

                char *buf;
                size_t cap;
                size_t used = 0;
                bool full = false;
            };

            void head(Writer &w, const Stamp &stamp)
            {
                w.text("{\"ts\":");
                w.number(stamp.ms);
            }

            // Both readable and at least VOLTS_CHANGE apart, or one readable and
            // the other not.
            bool voltsChanged(float before, float now)
            {
                const bool a = std::isfinite(before);
                const bool b = std::isfinite(now);
                if (a != b)
                    return true;
                return a && std::fabs(now - before) >= VOLTS_CHANGE - 0.0001f;
            }

            const char *bit(bool value)
            {
                return value ? "true" : "false";
            }
        } // namespace

        Stamp stampOf(int64_t epochMs, uint32_t nowMs, uint32_t atMs)
        {
            constexpr int64_t CLOCK_VALID_MS = 1704067200LL * 1000; // Core::CLOCK_VALID_EPOCH
            const uint32_t ago = nowMs - atMs;
            Stamp stamp;
            if (epochMs >= CLOCK_VALID_MS)
            {
                stamp.ms = static_cast<uint64_t>(epochMs) - ago;
                stamp.epoch = true;
            }
            else
            {
                stamp.ms = atMs;
            }
            return stamp;
        }

        Kind Schedule::due(bool moving, float volts, uint32_t nowMs)
        {
            if (!on)
                return Kind::None;
            if (moving)
            {
                if (!moveSeen)
                    moveStarted = true;
                moveSeen = true;
                lastMovingMs = nowMs;
            }
            else if (moveSeen && nowMs - lastMovingMs > TAIL_MS)
            {
                moveSeen = false;
                moveStarted = false;
            }
            if (moveSeen)
                return moveStarted || !any || nowMs - lastMs >= MOVING_INTERVAL_MS ? Kind::Moving : Kind::None;
            if (restMs == 0)
                return Kind::None;
            if (!any || nowMs - lastMs >= restMs)
                return Kind::Rest;
            if (nowMs - lastMs >= VOLTS_CHANGE_MIN_MS && voltsChanged(lastVolts, volts))
                return Kind::Rest;
            return Kind::None;
        }

        void Schedule::published(Kind kind, float volts, uint32_t nowMs)
        {
            if (kind == Kind::None)
                return;
            any = true;
            lastMs = nowMs;
            lastVolts = volts;
            if (kind == Kind::Moving)
                moveStarted = false;
        }

        size_t writeSample(char *out, size_t size, const Sample &s, const Stamp &stamp)
        {
            Writer w(out, size);
            head(w, stamp);
            w.name("state", Roof::stateName(s.state));
            w.reading("pos", s.positionKnown ? s.positionPercent : NAN, 1);
            w.text(",\"hall\":%u", static_cast<unsigned>(s.hall));
            w.reading("volts", s.volts, 2);
            w.text(",\"duty\":%u", static_cast<unsigned>(s.dutyPercent));
            w.name("phase", s.phase);
            w.name("target", Roof::endName(s.target));
            w.name("origin", Diag::originName(s.origin));
            return w.finish();
        }

        size_t writeRest(char *out, size_t size, float volts, const Stamp &stamp)
        {
            Writer w(out, size);
            head(w, stamp);
            w.reading("volts", volts, 2);
            return w.finish();
        }

        size_t writeMove(char *out, size_t size, const Diag::Summary &m, const Stamp &stamp, bool currentSense)
        {
            Writer w(out, size);
            head(w, stamp);
            w.text(",\"id\":%lu", static_cast<unsigned long>(m.id));
            w.name("origin", Diag::originName(m.origin));
            w.name("target", Roof::endName(m.target));
            w.name("endState", Roof::stateName(m.endState));
            w.name("result", Diag::resultName(m.result));
            if (m.result == Diag::Result::Fault)
                w.name("fault", Roof::faultName(m.fault));
            else
                w.text(",\"fault\":null");
            w.text(",\"durationMs\":%lu", static_cast<unsigned long>(m.durationMs()));
            w.text(",\"drivenMs\":%lu", static_cast<unsigned long>(m.drivenMs));
            w.text(",\"hallStart\":%u", static_cast<unsigned>(m.hallStart));
            w.text(",\"hallEnd\":%u", static_cast<unsigned>(m.hallEnd));
            w.text(",\"hallPeak\":%u", static_cast<unsigned>(m.hallPeak));
            w.text(",\"hallPeakMs\":%lu", static_cast<unsigned long>(m.hallPeakMs));
            w.reading("restVolts", m.restVolts, 2);
            w.reading("minVolts", m.minVolts, 2);
            w.reading("sagVolts", m.sagVolts(), 2);
            if (currentSense)
            {
                w.reading("peakAmps", m.peakAmps, 2);
                w.reading("averageAmps", m.averageAmps, 2);
            }
            w.text(",\"fromUnknown\":%s", bit(m.fromUnknown));
            w.text(",\"blind\":%s", bit(m.blind));
            w.text(",\"calibration\":%s", bit(m.calibration));
            return w.finish();
        }
    } // namespace Telemetry
} // namespace SQM
