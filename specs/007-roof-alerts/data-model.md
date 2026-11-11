# Data Model: Roof Alerts

## Alert types (`lib/AlertLogic`, `Alerts::AlertType`)

The placeholder `Roof` is replaced by seven types, in this order after `ClientDisconnected`:

| Type | Name (payload `event`, settings key) | Critical | Default level | Variables (besides the common ones) |
|---|---|---|---|---|
| MountNotParked | `mount_not_parked` | yes | 4 | `reason`, `limit`, `details`, `reminder` |
| RoofNotClosed | `roof_not_closed` | yes | 4 | `reason`, `limit`, `details`, `reminder` |
| MoveFault | `move_fault` | no | 3 | `reason` |
| SupplyLow | `supply_low` | no | 2 | `reason` |
| UnsafeClosing | `unsafe_closing` | no | 2 | `reason` |
| RoofClosed | `roof_closed` | no | 2 | `reason` |
| SafetyLost | `safety_lost` | no | 2 | `reason` |

`Alerts::isCritical(type)` is true for the first two.

## Roof inputs to the engine (`Alerts::RoofInputs`, in `AlertInputs::roof`)

| Field | Meaning |
|---|---|
| `known` | the fields below are filled (false: the engine ignores the roof, e.g. the demo) |
| `faultLatched`, `fault` | a motion fault is latched, and its text |
| `supplyLow`, `supply` | the supply is below its minimum or unreadable, and its text |
| `sourceLost`, `source` | a weather source that is on is lost, and its text |
| `closing`, `weather` | the close sequence is attempting a close (WaitPark, Closing, RetryWait), and the weather text that started it |
| `closedCount` | closes after an attempt since start (`CloseSeq::closedAfterAttempt`) |
| `sourceKnown` | no weather source that is on still waits for its first reply |

## Rules (`Alerts::AlertRules`)

New: `onMoveFault`, `onSupplyLow`, `onUnsafeClosing`, `onRoofClosed`, `onSafetyLost`, from the
events' levels (level 0 = off). Unchanged: `cooldownSeconds`, `startupGraceSeconds` (60).
`ROOF_SETTLE_SECONDS` = 30 (supply, source). `roofCooldown(cooldownSeconds)` = at least
`ROOF_MIN_COOLDOWN_SECONDS` (60) for the four condition events.

## Engine state per condition event

A `Tracker` each for fault, supply, source and closing:

| State | Condition true | Condition false |
|---|---|---|
| not announced, not pending | pending since now | - |
| pending | announce when: level on, grace over, held for the settle time, cooldown since this event's last alert over | not pending |
| announced | nothing | not announced (silently) |
| level off | follows silently (announced = condition) | not announced |

`roof_closed`: `closedSeen` (the count last seen) and `closedPending`; a rise sets pending; pending
is sent after the grace when the level is on, dropped when it is off.

`announcedRoof()` / `restoreAnnouncedRoof(bits)`: bit 0 fault, bit 1 supply, bit 2 source, bit 3
closing; `announcedCheck`/`announcedValid` guard the RTC copy.

## Event settings (`AlertsConfig`)

Seven new `EventSetting` fields (`mountNotParked`, `roofNotClosed`, `moveFault`, `supplyLow`,
`unsafeClosing`, `roofClosed`, `safetyLost`), JSON `alerts.events.<name>` with `level` (0-4; 3-4 for
the critical two), `sound`, `title` (≤ 80 characters), `message` (≤ 240). Stored in the main alerts
part (NVS key `alertsB`); the imaging-app events stay in their own part.

## Critical alert finishing (`Core::finishCriticalAlert`)

In: the alert from the sequence (type, built-in title and message, variables `reason`, `limit`,
`details`, `reminder`), the settings, the clock. Out: the alert to dispatch.

1. Level: the event's level, at least Urgent.
2. Sound: the event's sound.
3. Title: the custom title rendered, else the built-in; with `reminder` = `yes`, "Still: " in front
   unless it is there already.
4. Message: the custom message rendered, else the built-in; a custom message with neither
   `{reason}` nor `{details}` gets "\n\n" and the built-in message added.
