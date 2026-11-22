#pragma once

// Spec 021: a settings save from a page loaded before the settings changed.
// The page sends the revision its form was loaded from (`baseRevision`) and the
// values it loaded for each field it changes (`baseValues`). A save at the
// current revision goes through; at another one, only if none of those values
// changed on the device meanwhile. Saves without `baseRevision` (scripts,
// older pages) are not checked.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace SQM
{
    namespace ConfigSave
    {
        constexpr size_t MAX_CONFLICTS = 16;

        struct Check
        {
            bool stale = false;
            // Dotted names of the base values that differ ("park.nina.url"), at most MAX_CONFLICTS.
            std::vector<std::string> conflicts;
        };

        // `body` is the save as sent; `currentJson` gives the settings as GET
        // /api/config shows them (Config::toJson(true): secrets masked), asked
        // for only when the revision isn't current. A missing key equals null,
        // numbers compare by value, arrays and strings whole. A body that isn't
        // an object is left to the settings' own checks.
        Check checkSave(const std::string &body, uint32_t currentRevision, const std::function<std::string()> &currentJson);
        Check checkSave(const std::string &body, uint32_t currentRevision, const std::string &currentJson);

        // "Settings changed on the device since this page loaded them: a, b".
        std::string conflictText(const Check &check);
    } // namespace ConfigSave
} // namespace SQM
