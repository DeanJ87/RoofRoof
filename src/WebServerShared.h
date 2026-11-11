#pragma once

// Constants shared by the WebServer sources (WebServer*.cpp).

#include <cstddef>
#include <cstdint>

namespace SQM::WebShared
{
    constexpr size_t CONFIG_JSON_BUFFER_SIZE = 8192; // full config incl. custom alert texts

    constexpr const char *ARMED_NVS_NAMESPACE = "roofroof-alerts";
} // namespace SQM::WebShared
