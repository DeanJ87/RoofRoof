#pragma once

// A settings save runs on a task of its own (fix 041).
//
// Merging, validating and storing the settings goes deep: the settings parse
// (a 3 KB frame), every section's checks and the analog checks' text. On the
// async_tcp task's 8 KB stack (spec 038), under the web server's own frames,
// a save overflowed it (crash records: "Flash write: in progress", stack left
// 0-8 B, in the analog check's snprintf). Here it starts on an empty stack,
// sized against the worst path tools/firmware/stack_depth.py finds in the
// build. The caller waits for it, as it waited for the save before.

#include <cstdint>
#include <functional>

namespace SQM
{
    namespace SaveTask
    {
        // tools/firmware/stack_depth.py checks the save path against this.
        constexpr uint32_t STACK_BYTES = 10 * 1024;

        // Runs `work` on its own task and waits until it has finished. False
        // (and `work` not run) when the task couldn't be started: memory short.
        bool run(const std::function<void()> &work);
        // The least stack left by the last run, in bytes (for the log).
        uint32_t lastStackLeft();
    } // namespace SaveTask
} // namespace SQM
