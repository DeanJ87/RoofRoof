#include "ConfigSave.h"

#include <ArduinoJson.h>
#include <cstring>

namespace SQM
{
    namespace ConfigSave
    {
        namespace
        {
            size_t capacityFor(const std::string &json)
            {
                return json.size() + 5120 > 8192 ? json.size() + 5120 : 8192;
            }

            bool sameValue(JsonVariantConst a, JsonVariantConst b);

            bool sameArray(JsonArrayConst a, JsonArrayConst b)
            {
                if (a.size() != b.size())
                    return false;
                for (size_t i = 0; i < a.size(); ++i)
                    if (!sameValue(a[i], b[i]))
                        return false;
                return true;
            }

            bool sameObject(JsonObjectConst a, JsonObjectConst b)
            {
                if (a.size() != b.size())
                    return false;
                for (JsonPairConst kv : a)
                    if (!sameValue(kv.value(), b[kv.key()]))
                        return false;
                return true;
            }

            bool sameValue(JsonVariantConst a, JsonVariantConst b)
            {
                if (a.isNull() || b.isNull())
                    return a.isNull() && b.isNull();
                if (a.is<bool>() || b.is<bool>())
                    return a.is<bool>() && b.is<bool>() && a.as<bool>() == b.as<bool>();
                if (a.is<double>() || b.is<double>())
                    return a.is<double>() && b.is<double>() && a.as<double>() == b.as<double>();
                if (a.is<const char *>() || b.is<const char *>())
                    return a.is<const char *>() && b.is<const char *>() && std::strcmp(a.as<const char *>(), b.as<const char *>()) == 0;
                if (a.is<JsonArrayConst>() || b.is<JsonArrayConst>())
                    return a.is<JsonArrayConst>() && b.is<JsonArrayConst>() && sameArray(a.as<JsonArrayConst>(), b.as<JsonArrayConst>());
                return a.is<JsonObjectConst>() && b.is<JsonObjectConst>() && sameObject(a.as<JsonObjectConst>(), b.as<JsonObjectConst>());
            }

            // Each leaf of `base` (objects are walked, anything else is a leaf).
            void collect(JsonVariantConst base, JsonVariantConst current, const std::string &path, std::vector<std::string> &out)
            {
                if (out.size() >= MAX_CONFLICTS)
                    return;
                if (base.is<JsonObjectConst>() && (current.isNull() || current.is<JsonObjectConst>()))
                {
                    for (JsonPairConst kv : base.as<JsonObjectConst>())
                        collect(kv.value(), current[kv.key()], path.empty() ? kv.key().c_str() : path + "." + kv.key().c_str(), out);
                    return;
                }
                if (!sameValue(base, current))
                    out.push_back(path);
            }
        } // namespace

        Check checkSave(const std::string &body, uint32_t currentRevision, const std::string &currentJson)
        {
            return checkSave(body, currentRevision, [&currentJson]() { return currentJson; });
        }

        Check checkSave(const std::string &body, uint32_t currentRevision, const std::function<std::string()> &currentJson)
        {
            Check check;
            DynamicJsonDocument sent(capacityFor(body));
            if (deserializeJson(sent, body) || !sent.is<JsonObject>())
                return check;
            JsonVariantConst revision = sent["baseRevision"];
            if (revision.isNull())
                return check; // not asked for: saved as before
            if (revision.is<uint32_t>() && revision.as<uint32_t>() == currentRevision)
                return check;
            check.stale = true;
            JsonVariantConst values = sent["baseValues"];
            if (!values.is<JsonObjectConst>())
                return check;
            const std::string shown = currentJson();
            DynamicJsonDocument current(capacityFor(shown));
            if (deserializeJson(current, shown))
                return check; // couldn't compare: refused, never saved blind
            collect(values, current.as<JsonVariantConst>(), "", check.conflicts);
            check.stale = !check.conflicts.empty();
            return check;
        }

        std::string conflictText(const Check &check)
        {
            std::string text = "Settings changed on the device since this page loaded them";
            for (size_t i = 0; i < check.conflicts.size(); ++i)
                text += (i == 0 ? ": " : ", ") + check.conflicts[i];
            return text;
        }
    } // namespace ConfigSave
} // namespace SQM
