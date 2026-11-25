# Research: Spec 030

## R1 Where the hold goes

The verdict feeds three things: the gates (Open refused, an open stopped), the status, and the
automatic close. Holding the verdict itself would delay the first two, which must stay immediate
(constitution III). The hold therefore filters only the close sequence's `weatherSafe` input, in
`CloseOnUnsafe::handle` after spec 027's override mask. The sequence's own pause and resume (spec
005) and its re-arm spell are unchanged.

## R2 Summed, not continuous

A continuous-run hold never fires on a verdict that flaps faster than the hold, which would leave a
roof open in genuinely changeable weather. Summing the unsafe time, forgotten only after a safe spell
as long as the hold, fires on flapping (one close in about two minutes at a 5 s flap) and ignores a
lone blip. Keeping it fired until that same safe spell stops a flapping verdict from pausing and
resuming the close it started.

## R3 Silent SQMeter timing

SQMeter is polled every 5 s; the source turns stale after `safety.staleSeconds` (30 s) without a good
reply. A 15 s SQMeter restart never reaches stale. A real outage: stale at 30-35 s, then the 60 s
hold: the close starts at about 90-95 s.

## R4 Flash

Measured on main 448f16b: 1,728,496 B (94.19%). Already in use: `-Os`, `-ffunction-sections`,
`-fdata-sections`, `--gc-sections`, no C++ exceptions, the common-CA bundle, trimmed mbedTLS options.
The framework's link line carries `-fno-lto`. LTO over everything compiled here (framework, lib_deps
and ours) measured 1,583,680 B (86.3%); LTO over the project's own src/ and lib/ only measured
1,618,080 B (88.2%, -110,416 B). The project's own code has no interrupt handlers and no IRAM
functions (esp_timer callbacks run on the timer task), so moving code between its functions can't
place flash code where the cache may be disabled; the RTC_NOINIT and RTC_DATA variables keep their
sections (checked with nm). The framework and lib_deps keep their usual compile. Chosen: own code
only, `scripts/lto.py` (a build middleware adds `-flto=auto` to objects under src/ and lib/, and the
link drops `-fno-lto`). Other candidates (Arduino core log level, ArduinoJson features, the
certificate bundle) were left: each is a few KB and changes behaviour or needs a framework rebuild.

## R5 Config.h

At 600 lines. `AutoCloseConfig` moves to `ConfigAutoClose.h` (included by Config.h), freeing 37 lines.

## R6 Stall sign without current sense

The 12 V supply sags about 1.1 V while the motor runs (owner's measurement). A stalled motor draws
its stall current, several times the running current, so the sag deepens. The rest voltage is taken
just before the drive; the first second (the inrush) is not judged; a sag of 2.0 V held 500 ms stops
the blind close. The supply minimum (11.0 V, 250 ms) still faults as for every move.

## R7 One blind close per fault

A board restarting in the rain with a dead Hall sensor would otherwise blind-close at every boot. The
fault record (16 bytes, spec 003) has spare bytes; a flag there survives restarts and power cuts and
is cleared with the fault.

## R8 Alert persistence and flash wear

NVS writes only when a critical alert is queued undelivered and when it is delivered: at most a few
per incident, coalesced per event type, from the main loop. Spec 029 is looking at flash writes and
interrupts; nothing here writes from an interrupt or a timer callback.
