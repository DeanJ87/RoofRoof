# Research: Roof Alerts

Each decision lists what was chosen, why, and what else was considered. The owner was not
available; open choices took the safest option the constitution allows (spec "Assumptions decided
for the owner").

## R1. Two paths: critical events stay on spec 005's path

- **Decision**: the two critical events keep spec 005's path unchanged in shape: the close
  sequence decides (its own throttle and reminders), `CloseOnUnsafe` hands the alert to the sink,
  the sink (`WebServer::raiseRoofAlert`) dispatches it past the pause, grace and master switch,
  and `AlertRetry` sends it again until a channel takes it. What changes is the alert itself: a
  per-event type instead of `roof`, and the event's settings (level clamped to urgent or above,
  sound, wording with the reason kept) applied in the sink by a pure function
  (`Core::finishCriticalAlert`). The five other events go through the alert engine
  (`Alerts::AlertEngine`) with the imaging-app events, so they share the grace, cooldown,
  stacking, pause and master switch.
- **Rationale**: spec 005's delivery guarantees (bypass, throttle, reminders, resend with back-off,
  RTC-kept stand-down) were reviewed six times; moving the critical events into the engine would
  re-open all of them. The non-critical events need exactly what the engine already gives the
  imaging-app events.
- **Alternatives**: every roof event through the engine with a "critical" flag (rejected: the engine
  has no delivery confirmation or reminders, and its grace and cooldown would have to be bypassed
  per type, mixing two rule sets in one place); every roof event through the sink (rejected: the
  sink would need its own grace, cooldown and stacking, a second copy of the engine).

## R2. Mapping spec 005's events

| Sequence event | Cause | Alert type | Key |
|---|---|---|---|
| ParkNotConfirmed | any | MountNotParked | `mount_not_parked` |
| CloseFailed | any | RoofNotClosed | `roof_not_closed` |
| CannotClose | any | RoofNotClosed | `roof_not_closed` |
| StoppedByPerson | - | RoofNotClosed | `roof_not_closed` |
| Started, EndedSafe, Closed | - | none on this path (see R4) | - |

The built-in title and message stay `CloseSeq::alertFor`'s, so spec 005's wording tests hold.

## R3. Critical level floor and wording

- **Decision**: level 3 (urgent) or 4 (wake me), default 4. `Config::validate` refuses 0-2 for
  the two critical keys ("Alerts: the roof's critical alerts are urgent or wake me"); the web schema
  has the same rule and the row offers only those two levels. `finishCriticalAlert` sends any lower
  stored level as urgent (belt and braces: a config written before the rule, or a future bug).
- Custom wording: rendered with the usual variables plus `{reason}` (the park result's, fault's or
  refusal's text, or the roof state), `{limit}` (the time limit that ran out, "240 s", or ""),
  `{details}` (the full built-in message). A custom message that contains neither `{reason}` nor
  `{details}` gets a blank line and the built-in message added. A reminder (the sequence's
  `Decision::reminder`) carries the variable `reminder` = `yes`; the title then starts with
  "Still: " whatever the template says.
- **Rationale**: constitution II/III require the alert and its reason; "normal" can be silenced by
  Pushover's quiet hours.
- **Alternatives**: no custom wording for critical events (rejected: SQMeter's design offers it for
  every event and the reason is kept anyway); allowing off with a warning (rejected: the
  constitution names no exception).

## R4. The non-critical events in the engine

All five take their inputs from one `Alerts::RoofInputs` the main loop fills each step of the close
sequence (every 100 ms) and hands to the web server, which runs the engine once a second.

| Event | Condition | Settle | Cooldown | At start-up |
|---|---|---|---|---|
| `move_fault` | a fault is latched | 0 | yes | announced after the grace unless kept as announced |
| `supply_low` | `RoofIo::supplyLow(snapshot)` | 30 s | yes | same |
| `safety_lost` | a source on and Stale/Unreachable/Unreadable/Offline, or the fitted rain contact Unreadable | 30 s | yes | same |
| `unsafe_closing` | the sequence's phase is WaitPark, Closing or RetryWait | 0 | yes | after the grace if still attempting |
| `roof_closed` | the sequence's count of `Event::Closed` went up | - | no | held through the grace |

- Condition events fire on the rising edge only: when the condition ends, the tracker resets
  silently, so "back to normal" is never announced (out of scope) and the next rising edge alerts
  again, subject to the cooldown measured from that event's last alert. A rising edge inside the
  cooldown is held and sent when the cooldown ends if the condition still holds (SQMeter's rule: a
  held change is not lost).
- While the event's level is off, the tracker follows the condition silently (switching the level
  on doesn't announce an old condition), as the imaging-app rules do.
- `roof_closed` keeps one pending flag: set when the count rises, sent when the grace has ended
  (dropped if the level is off).
- **Rationale**: one alert per real change, bounded by the settle time and the cooldown, and never a
  storm from a flapping input.

## R5. Not repeating after a reset

- **Decision**: the engine reports which of `move_fault`, `supply_low`, `safety_lost` it has
  announced and that still hold (three bits); the web server keeps them in RTC memory (a magic
  number and a check word, like spec 005's record) and restores them before the first engine pass.
  A restored bit marks the tracker as announced; it is cleared as soon as the condition is seen
  absent.
- **Rationale**: a reset loop with a latched fault would otherwise send the same alert after every
  reset (one per 60 s grace). RTC memory costs nothing to write and survives a reset or crash but
  not a power cut, after which the alert going out again is the safe outcome.
- **Alternatives**: NVS (rejected: a write per change wears flash and stalls the roof's task); no
  memory (rejected: a reset loop storm).

## R6. Texts (constitution III)

- `move_fault`: `RoofIo::describeFault` (value and limit, or the input), e.g. "Timeout 61.0 s > 60.0 s".
- `supply_low`: "Supply 10.9 V < 11.0 V" or "Supply unreadable (minimum 11.0 V)", through the
  existing `supplyText`, now public as `RoofIo::describeSupply`.
- `safety_lost`: `Safety::describeSource` for each lost SQMeter source, "Rain contact unreadable"
  for the contact, joined with "; ".
- `unsafe_closing` and `roof_closed`: `Safety::describe(verdict)`, taken while the sequence is
  attempting (the weather that started it), kept for the close.

## R7. Settings storage

- **Decision**: seven new `AlertsConfig::EventSetting` fields, persisted under `alerts.events` in the
  main alerts part (the imaging-app events keep their own part). `EVENT_COUNT` 10,
  `FIRST_CLIENT_EVENT` 7. The existing 3,900-byte check per stored part still applies: seven events
  with defaults add about 430 bytes; long custom wording on all of them is refused with the existing
  "too long in total" message.

## R8. Where the code lives (parallel specs)

- `lib/AlertLogic`: the type list, rules and `RoofInputs` in `AlertEngine.h`; the roof trackers in a
  new `src/RoofEvents.cpp` so spec 006's changes to the imaging-app code in `AlertEngine.cpp` don't
  collide.
- `lib/CloseSequenceLogic`: a new `RoofAlerts.h/.cpp`: the sequence-to-alert mapping and the roof
  conditions from the snapshot and verdict.
- `lib/DeviceCore`: rules, settings lookup, `finishCriticalAlert`, samples, ntfy tags.
- Glue: `CloseOnUnsafe` (sends typed alerts, keeps the conditions), one line in `main.cpp`, the web
  server's alert file. Nothing in the Alpaca router (006) or the restart and OTA holds (010).
