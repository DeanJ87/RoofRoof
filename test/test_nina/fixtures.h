#pragma once

// N.I.N.A. Advanced API v2 replies. Built from the plugin's OpenAPI spec
// (ninaAPI/api_spec.yaml, 2.2.15) and its mount controller
// (WebService/V2/Equipment/Mount.cs): no live capture yet
// (specs/004-safety-inputs/research.md R2, "Not verified"). The info replies
// carry a realistic subset of MountInfo's fields; the parser must ignore the rest.

namespace Fixtures
{
    // Mount connected and parked (field order as in the MountInfo schema).
    inline const char *const INFO_PARKED =
        R"({"Response":{"TrackingMode":"Stopped","SiderealTime":5.2,"RightAscension":4.1,"Declination":89.9,)"
        R"("SiteLatitude":51.5,"SiteLongitude":-0.1,"SiteElevation":30,"Coordinates":{"RA":4.1,"RAString":"04:06:00",)"
        R"("RADegrees":61.5,"Dec":89.9,"DecString":"89° 54' 00\"","Epoch":"JNOW","DateTime":{"Now":"2026-11-11T22:00:00",)"
        R"("UtcNow":"2026-11-11T22:00:00Z"}},"SideOfPier":"pierEast","Altitude":51.5,"Azimuth":0.0,"AtPark":true,)"
        R"("TrackingEnabled":false,"AtHome":false,"CanFindHome":true,"CanPark":true,"CanSetPark":true,)"
        R"("Slewing":false,"CanSlew":true,"Connected":true,"Name":"EQ6-R","DisplayName":"EQ6-R",)"
        R"("DeviceId":"ASCOM.EQMOD.Telescope"},"Error":"","StatusCode":200,"Success":true,"Type":"API"})";

    inline const char *const INFO_TRACKING =
        R"({"Response":{"AtPark":false,"TrackingEnabled":true,"CanPark":true,"Slewing":false,"Connected":true,)"
        R"("Name":"EQ6-R","DisplayName":"EQ6-R","DeviceId":"ASCOM.EQMOD.Telescope"},)"
        R"("Error":"","StatusCode":200,"Success":true,"Type":"API"})";

    // Parking in progress: N.I.N.A. slews to the park position.
    inline const char *const INFO_SLEWING =
        R"({"Response":{"AtPark":false,"Slewing":true,"Connected":true,"Name":"EQ6-R","DisplayName":"EQ6-R",)"
        R"("DeviceId":"ASCOM.EQMOD.Telescope"},"Error":"","StatusCode":200,"Success":true,"Type":"API"})";

    // No mount connected: only the required fields are certain.
    inline const char *const INFO_DISCONNECTED = R"({"Response":{"Connected":false,"Name":"","DisplayName":"","DeviceId":""},)"
                                                 R"("Error":"","StatusCode":200,"Success":true,"Type":"API"})";

    inline const char *const INFO_UNKNOWN_ERROR =
        R"({"Response":"","Error":"Unknown error","StatusCode":500,"Success":false,"Type":"API"})";

    inline const char *const INFO_MISSING_AT_PARK =
        R"({"Response":{"Slewing":false,"Connected":true},"Error":"","StatusCode":200,"Success":true,"Type":"API"})";

    inline const char *const INFO_AT_PARK_STRING =
        R"({"Response":{"AtPark":"true","Slewing":false,"Connected":true},"Error":"","StatusCode":200,"Success":true,"Type":"API"})";

    inline const char *const INFO_MISSING_CONNECTED =
        R"({"Response":{"AtPark":true,"Slewing":false},"Error":"","StatusCode":200,"Success":true,"Type":"API"})";

    inline const char *const INFO_RESPONSE_NOT_OBJECT = R"({"Response":"","Error":"","StatusCode":200,"Success":true,"Type":"API"})";

    inline const char *const INFO_NO_SUCCESS = R"({"Response":{"AtPark":true,"Slewing":false,"Connected":true}})";

    inline const char *const INFO_TRUNCATED = R"({"Response":{"AtPark":true,"Slewing":false,"Conn)";

    inline const char *const NOT_JSON = "<html><body>404 Not Found</body></html>";

    inline const char *const PARK_PARKING = R"({"Response":"Parking","Error":"","StatusCode":200,"Success":true,"Type":"API"})";

    inline const char *const PARK_ALREADY =
        R"({"Response":"Mount already parked","Error":"","StatusCode":200,"Success":true,"Type":"API"})";

    inline const char *const PARK_NOT_CONNECTED =
        R"({"Response":"","Error":"Mount not connected","StatusCode":409,"Success":false,"Type":"API"})";

    inline const char *const PARK_UNEXPECTED_TEXT = R"({"Response":"Unparking","Error":"","StatusCode":200,"Success":true,"Type":"API"})";
} // namespace Fixtures
