#pragma once

// The settings as the JSON document GET /api/config sends (Config::toJson
// serialises the same). Separate from Config.h so the firmware can fill a
// document it allocated itself and check every allocation (spec 029 research
// R13: GET /api/config under load).

#include <ArduinoJson.h>
#include <cstddef>

#include "Config.h"

namespace SQM
{
    namespace ConfigJson
    {
        // The pool every setting fits in, with the longest values (test_config_safety,
        // test_roof_alert_settings, test_config_json_body); spec 023 added the pins and
        // analog groups, spec 027 an event, spec 046 mqtt.telemetry.
        constexpr size_t CAPACITY = 10240;
        // The pool Config::applyJson parses settings into: the text's size plus
        // PARSE_EXTRA_BYTES (parsing copies every string), at least PARSE_MIN_BYTES.
        constexpr size_t PARSE_EXTRA_BYTES = 7168;
        constexpr size_t PARSE_MIN_BYTES = 9216;
        inline size_t parseCapacity(size_t textBytes)
        {
            return textBytes + PARSE_EXTRA_BYTES > PARSE_MIN_BYTES ? textBytes + PARSE_EXTRA_BYTES : PARSE_MIN_BYTES;
        }

        // Fills `doc` with the settings; secrets masked with `redactSecrets`.
        void write(const Config &cfg, JsonDocument &doc, bool redactSecrets, bool includeAlerts = true);
    } // namespace ConfigJson
} // namespace SQM
