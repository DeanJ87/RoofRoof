# Research: Daylight Failsafe

## R1. Where the sun comes from

**Decision**: Copy SQMeter's `lib/AlertLogic/src/SunPosition.cpp` and `include/SunPosition.h`
(SQMeter `877208b`, NOAA's solar position approximation) unchanged into a new
`lib/DaylightLogic`, with SQMeter's `test_sun_position` reference values.

**Rationale**: RoofRoof removed the file with SQMeter's sky alerts (heritage: "Sun position and
darkness rules for alerts"). It is pure, small (about 50 lines), accurate to well under a degree
and already tested. Putting it back into `lib/AlertLogic` would tie the roof's daylight decision
to the alert library; a daylight library keeps the decision with its own tests (constitution V).

**Alternatives**: a table of sunrise times (needs the date and the site anyway, less exact);
asking N.I.N.A. or SQMeter (the failsafe must not depend on the device whose failure it covers,
constitution III's spirit).

## R2. The threshold and its band

**Decision**: day at or above `daylight.sunAltitudeDeg`; night again only below it minus 0.5°.
Default 0°, range -18° to +30°. Evaluated once a second.

**Rationale**: SQMeter's `nightSunAltitudeDeg` uses the sun's centre too (-0.833° sunset, -12°
nautical, -18° astronomical). Near the horizon the sun moves 0.05° to 0.25° a minute, so 0.5° is
2 to 10 minutes: enough that a computed value at the boundary can't flicker, short enough not to
delay the close meaningfully. -18° allows "close at astronomical dawn"; +30° covers a high
horizon. Once a second is far below the sun's rate and costs one trigonometric evaluation.

## R3. Location and clock unknown

**Decision**: state `unknown` (with which input is missing): no daylight close, no refusal, no
"open in daylight" alert. With the mode on, the alert engine raises `daylight_unknown` once the
state has been unknown for 600 s without a break.

**Rationale**: guessing could close at night or refuse every open. The clock is NTP only (spec
001 R11); a restart leaves it unset for a few seconds, so the 600 s settle avoids an alert after
every restart while still telling an owner whose LAN has no NTP. The clock counts as set from
`Core::CLOCK_VALID_EPOCH` (2024-01-01), as the rest of the firmware does.

## R4. One sequence, two reasons to close

**Decision**: extend `CloseSeq::Inputs` with `daylight` (daylight wants the roof closed). Inside
`Sequence::step`, the sequence acts on "a close is wanted" = weather not safe OR daylight, in
every place it used "weather not safe": starting an attempt, ending one (`EndedSafe`), the
10-minute re-arm of a stand-down. It tracks whether the episode is daylight-only. When the
weather stops being safe during a daylight-only episode, the episode becomes a weather episode;
if the sequence is then stood down by a person, halted or in its back-off, it re-arms first (goes
to Idle without an event) so the weather attempt starts on the same step, unless a person's stop
is seen on that same step. Every decision carries the episode (`Decision::daylight`), so alert
wording and the alert events can say which.

**Rationale**: a second sequence would issue its own park requests and closes, racing the first;
the whole value of reusing spec 005 is its tested time limits, stand-downs, restart record and
alert throttling. With `daylight` false (mode off or alert, or not day) the new code reduces to
spec 005 exactly: "wanted" is "weather not safe", the episode is never daylight-only, the re-arm
never fires. Every spec 005 test runs unchanged.

**Alternatives**: feeding daylight in as an unsafe verdict (rejected: it would change the
verdict, which constitution III forbids from anything but the weather sources, and the weather
alerts would fire for daylight); a separate daylight sequence (rejected above).

## R5. The open refusal

**Decision**: `RoofIo::GateInputs` gains `daylightCloses` and `daylightText`, handed over with the
verdict and park every 250 ms. `gateCommand(Open)` refuses with `GateRefusal::Daylight`
(`daylight`) when daylight closes and the weather is safe (the weather refusal wins);
`openMustStop` also stops an open under way when daylight closes. One rule serves the web UI,
Alpaca, MQTT and Home Assistant (spec 006's `checkCommand`), the alpaca simulator and the demo.

**Rationale**: as R4, an accepted open would be followed at once by a park request and a close.
The refusal names the sun's altitude and the threshold (constitution III, IV: an Alpaca error
saying why).

## R6. Alert events

**Decision**: three non-critical `AlertType`s, `DaylightOpen`, `DaylightClosed`,
`DaylightUnknown`, with trackers in `RoofEvents.cpp` (settle 30 s, 0, 600 s) and announced bits 4
and 5 kept across a reset like the other roof conditions. The daylight close's failures raise the
existing critical types through spec 005's path, with the daylight wording chosen by
`Decision::daylight`. `UnsafeClosing` and `RoofClosed` count weather episodes only.

**Rationale**: spec 007's design: one row each in Settings → Alerts, the usual rules, tests per
event. The critical events keep their guarantees without new types.

## R7. Restart record

**Decision**: the RTC record's `requested` byte becomes flags (bit 0 requested, bit 1
daylight-only episode); the magic's low byte is bumped, so a record from an older image is
ignored (as for any layout change).

**Rationale**: a daylight stand-down must survive a reset, and the weather re-arm needs to know
the stood-down episode was daylight-only. No new bytes; the checksum covers the byte.

## R8. Configuration size

**Decision**: `daylight` (`mode`, `sunAltitudeDeg`) and `location` (`set`, `latitude`,
`longitude`) in the main settings document; the three event settings in the main alerts part.

**Rationale**: about 110 bytes in the main document and 90 in the alerts part at their defaults;
both stay within `MAX_PERSISTED_JSON_BYTES` and `MAX_ALERTS_JSON_BYTES` (checked by the existing
size tests with every template at its longest).

## R9. Demo

**Decision**: the demo's emulated roof (`tools/demo-core/emulated_roof.cpp`) runs the same
`Daylight::Watch` and passes its decision to the sequence and the gates, as the firmware glue
does; a "Sun up" injector forces the altitude to the threshold plus 10° whatever the location and
clock.

**Rationale**: the demo must exercise the device's own logic (spec 011); forcing the altitude
avoids needing a location and a time of day in a browser demo.
