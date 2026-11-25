#pragma once

// Spec 027: Settings' N.I.N.A. Test (POST /api/nina/test): one read of the
// mount at the address typed, answered as reachable, mount connected, parked
// and slewing, or why not. The request itself is the firmware's (on its own
// task); this decides the URL and the answer (constitution V).

#include <ArduinoJson.h>

#include <string>

#include "NinaApi.h"

namespace SQM
{
    namespace Nina
    {
        // The mount info URL under N.I.N.A.'s address (a trailing '/' allowed).
        std::string testUrl(const std::string &base, const Api &api);
        // The reading from a reply (`httpStatus` > 0) or a failed request (`error`).
        MountReading testReading(const Api &api, int httpStatus, const std::string &body, const std::string &error, uint64_t nowMs);
        // {"reachable", "connected", "atPark", "slewing"} and, unless good,
        // "error"; returns the HTTP status for the answer (200 good, 502 not).
        int writeTestAnswer(JsonObject out, const MountReading &reading);
    } // namespace Nina
} // namespace SQM
