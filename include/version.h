#pragma once

// Firmware version information
// Local builds: the next release plus "+dev", so version comparisons order them
// correctly, and (spec 027) the commit: "0.1.0+dev.f3c98f6". CI stamps the tag
// (without "v") on release builds, which carry no commit; bump this after each
// release. No release has been tagged yet: 0.1.0 is the first.
#if __has_include("version_commit.h")
#include "version_commit.h"
#endif
#ifndef FIRMWARE_VERSION_SUFFIX
#define FIRMWARE_VERSION_SUFFIX ""
#endif
#define FIRMWARE_VERSION "0.1.0+dev" FIRMWARE_VERSION_SUFFIX
#define FIRMWARE_BUILD_DATE __DATE__
#define FIRMWARE_BUILD_TIME __TIME__
#define FIRMWARE_NAME "RoofRoof"

// Helper to get full version string
inline const char *getFirmwareVersion()
{
    static char version[64];
    snprintf(version, sizeof(version), "%s v%s", FIRMWARE_NAME, FIRMWARE_VERSION);
    return version;
}

// Helper to get build timestamp
inline const char *getBuildTimestamp()
{
    static char timestamp[64];
    snprintf(timestamp, sizeof(timestamp), "%s %s", FIRMWARE_BUILD_DATE, FIRMWARE_BUILD_TIME);
    return timestamp;
}
