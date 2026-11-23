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

            enum class Kind : uint8_t
            {
                Null,
                Bool,
                Number, // integers and floats alike: 1850 equals 1850.0
                Text,
                Array,
                Object,
            };

            Kind kindOf(JsonVariantConst v)
            {
                if (v.isNull())
                    return Kind::Null;
                if (v.is<bool>())
                    return Kind::Bool;
                if (v.is<double>())
                    return Kind::Number;
                if (v.is<const char *>())
                    return Kind::Text;
                return v.is<JsonArrayConst>() ? Kind::Array : Kind::Object;
            }

            // Values of different kinds never match ("1" isn't 1).
            bool sameValue(JsonVariantConst a, JsonVariantConst b)
            {
                const Kind kind = kindOf(a);
                if (kind != kindOf(b))
                    return false;
                switch (kind)
                {
                case Kind::Null:
                    return true;
                case Kind::Bool:
                    return a.as<bool>() == b.as<bool>();
                case Kind::Number:
                    return a.as<double>() == b.as<double>();
                case Kind::Text:
                    return std::strcmp(a.as<const char *>(), b.as<const char *>()) == 0;
                case Kind::Array:
                    return sameArray(a.as<JsonArrayConst>(), b.as<JsonArrayConst>());
                case Kind::Object:
                    break;
                }
                return sameObject(a.as<JsonObjectConst>(), b.as<JsonObjectConst>());
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

            // A deserialisation filter keeping only what `base` names: a leaf keeps
            // the device's value whole, an object keeps the keys it names.
            void keepOnly(JsonObjectConst base, JsonObject filter)
            {
                for (JsonPairConst kv : base)
                {
                    if (kv.value().is<JsonObjectConst>())
                        keepOnly(kv.value().as<JsonObjectConst>(), filter.createNestedObject(kv.key()));
                    else
                        filter[kv.key()] = true;
                }
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
            // Only the compared fields are kept: their structure is no bigger than
            // what was sent, their text no longer than the whole document.
            DynamicJsonDocument filter(values.memoryUsage() + 512);
            keepOnly(values.as<JsonObjectConst>(), filter.to<JsonObject>());
            const std::string shown = currentJson();
            DynamicJsonDocument current(sent.memoryUsage() + shown.size() + 1024);
            if (filter.overflowed() || deserializeJson(current, shown, DeserializationOption::Filter(filter)))
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
