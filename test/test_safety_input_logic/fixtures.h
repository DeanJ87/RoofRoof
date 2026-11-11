#pragma once

// SQMeter /api/safety documents. The first two are verbatim from SQMeter
// (tools/api/examples/safety.json and docs/api/rest.md, SQMeter spec 013);
// the rest are broken on purpose.

namespace Fixtures
{
    inline const char *const SAFETY_SAFE = R"({
  "safe": true,
  "rawSafe": true,
  "alpacaEnabled": true,
  "reasonFlags": 0,
  "reasons": [],
  "secondsUntilSafe": 0,
  "rulesNotInEffect": [],
  "evaluatedAgeMs": 0,
  "changedAgeMs": 2005
})";

    inline const char *const SAFETY_RAIN = R"({
  "safe": false,
  "rawSafe": false,
  "alpacaEnabled": true,
  "reasonFlags": 512,
  "reasons": ["Rain detected"],
  "secondsUntilSafe": 0,
  "rulesNotInEffect": [],
  "evaluatedAgeMs": 412,
  "changedAgeMs": 1260000
})";

    // An older SQMeter without the age fields.
    inline const char *const SAFETY_NO_AGE = R"({"safe": true, "reasonFlags": 0})";
    inline const char *const SAFETY_SAFE_WITH_FLAGS = R"({"safe": true, "reasonFlags": 16, "evaluatedAgeMs": 10})";
    inline const char *const SAFETY_MISSING_SAFE = R"({"reasonFlags": 0, "evaluatedAgeMs": 10})";
    inline const char *const SAFETY_SAFE_AS_STRING = R"({"safe": "true", "reasonFlags": 0})";
    inline const char *const SAFETY_SAFE_NULL = R"({"safe": null, "reasonFlags": 0})";
    inline const char *const SAFETY_MISSING_FLAGS = R"({"safe": true})";
    inline const char *const SAFETY_NEGATIVE_FLAGS = R"({"safe": true, "reasonFlags": -1})";
    inline const char *const SAFETY_FRACTION_FLAGS = R"({"safe": true, "reasonFlags": 1.5})";
    inline const char *const SAFETY_BAD_AGE = R"({"safe": true, "reasonFlags": 0, "evaluatedAgeMs": "soon"})";
    inline const char *const SAFETY_ARRAY = R"([true, 0])";
    inline const char *const SAFETY_TRUNCATED = R"({"safe": true, "reasonFl)";
    inline const char *const NOT_JSON = "<html>Not Found</html>";
} // namespace Fixtures
