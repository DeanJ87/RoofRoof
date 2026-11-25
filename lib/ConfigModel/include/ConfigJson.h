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
        // test_roof_alert_settings); spec 023 added the pins and analog groups, spec 027 an event.
        constexpr size_t CAPACITY = 9728;

        // Fills `doc` with the settings; secrets masked with `redactSecrets`.
        void write(const Config &cfg, JsonDocument &doc, bool redactSecrets, bool includeAlerts = true);
    } // namespace ConfigJson
} // namespace SQM
