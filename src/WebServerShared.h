#pragma once

// Constants shared by the WebServer sources (WebServer*.cpp).

#include <cstddef>
#include <cstdint>

class AsyncWebServerRequest;

namespace SQM::WebShared
{
    // The request came in over the setup hotspot (src/WebServer.cpp).
    bool arrivedViaHotspot(AsyncWebServerRequest *request);

    constexpr size_t CONFIG_JSON_BUFFER_SIZE = 8192; // full config incl. custom alert texts

    constexpr const char *ARMED_NVS_NAMESPACE = "roofroof-alerts";
} // namespace SQM::WebShared
