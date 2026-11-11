# Data Model: Roof Hardware

The pure types live in `namespace SQM::RoofIo` (`lib/RoofIoLogic`); the firmware glue is
`SQM::RoofControl` (`src/RoofControl*.cpp`). Spec 002's types (`SQM::Roof`) and spec 004's
(`SQM::Safety`) are used unchanged.

## Fixed values (`RoofIoLogic`)

| Name | Value | Why |
|---|---|---|
| `CONTROL_PERIOD_MS` | 20 | spec 002's rate (Hall confirmation, step-gap limit) |
| `WATCHDOG_LIMIT_MS` | 100 (= `Roof::MAX_STEP_GAP_MS`) | the longest unsupervised drive |
| `WATCHDOG_CHECK_MS` | 10 | trips within 110 ms of the last step |
| `HALL_SAMPLES` | 5 | median per step |
| `ANALOG_SAMPLES` | 4 | averaged supply and current samples |
| `SUPPLY_DIVIDER` | 122 / 22 | 100k/22k (pin table) |
| `CURRENT_DIVIDER` | 30 / 20 | 10k/20k (pin table) |
| `INPUT_CONFIRM_READINGS` | 3 | equal readings in a row before a switch level counts |
| `INPUT_CHATTER_MS` | 1000 | no three equal readings for this long: unreadable |
| `STATUS_BROADCAST_MIN_MS` | 250 | live update after a change, at most this often |
| `SAFETY_HANDOVER_STALE_MS` | 5000 | verdict and park result older than this: not safe, not parked |
| `CURRENT_SAMPLES` | 8 | current samples per step, spread over several PWM periods |
| `WATCHDOG_BEAT_LIMIT_MS` | 50 | EN may only go high while the watchdog's last check is this fresh |
| `CURRENT_SETTLE_MS` | 1000 | driving this long before `roof.minRunningAmps` is checked |
| `FLASH_STALE_MS` | 5000 | a flash writer with no progress for this long no longer blocks |
| `COMMAND_HEADER` | `X-RoofRoof` | must be `1` on every roof command |
| `PWM_FREQUENCY_HZ`, `PWM_RESOLUTION_BITS` | 20000, 8 | firmware only (`MotorDriver`) |

## Readings

| Function | In | Out |
|---|---|---|
| `medianCounts(samples, n)` | 1-9 raw counts | the median (the lower middle for even n) |
| `averageMillivolts(samples, n)` | 1-9 mV | the mean |
| `supplyVolts(pinMv)` | mV at GPIO36 | V at the supply; NaN for a non-finite input |
| `motorAmps(rightMv, leftMv, ampsPerVolt)` | mV at GPIO34/35 | A; NaN if both inputs are non-finite |

## Switch inputs: `Contact`, `SwitchInput`

`Contact`: `Unreadable` (`unreadable`), `Closed` (`closed`), `Open` (`open`).
`SwitchInput::update(bool levelLow, uint32_t nowMs) -> Contact`: a level counts after
`INPUT_CONFIRM_READINGS` equal readings in a row; `Unreadable` before the first and after
`INPUT_CHATTER_MS` without three equal readings in a row. Low = `Closed`.

Mapping to spec 004: `parkReading(Contact)`: Closed → `SwitchReading::Closed`, Open → `Open`,
Unreadable → `Unreadable`. `rainReading(Contact)`: Closed → `RainContact::Raining`, Open → `Dry`,
Unreadable → `Unreadable`.

## Fault record: `FaultCause`, `FaultRecord`

`FaultCause`: `Logic` (`logic`: spec 002 decided it), `Watchdog` (`watchdog`), `ParkInterlock`
(`parkInterlock`), `StoredUnreadable` (`storedUnreadable`), `WatchdogStopped` (`watchdogStopped`:
the watchdog's heartbeat age in ms / 50 ms). The three non-logic causes are latched
as `Roof::FaultKind::External`.

| Cause | measured | limit |
|---|---|---|
| `watchdog` | the gap since the last step, ms | 100 ms |
| `parkInterlock` | bits: 1 park result not parked, 2 switch 1 not closed, 4 switch 2 not closed | 0 |
| `storedUnreadable` | 0 | 0 |

Stored as a 16-byte NVS blob (`roofroof` / `roofFault`): bytes 0-1 `'R' 'F'`, 2 version 1, 3 kind,
4 cause, 5 reserved (0), 6-9 measured (float, little-endian), 10-13 limit (float), 14-15 a 16-bit
checksum of bytes 0-13 (the sum, inverted). `decodeFault`: length 0 → `None`; a valid record with a kind other than
None → `Fault`; anything else → `Unreadable` (latched as `storedUnreadable`).

## Gates: `GateInputs`, `GateRefusal`

`GateInputs`: the weather verdict and its text, the park result's `parked` and its text, the fitted
switch count (0-2) and the control task's switch readings.

| Function | Rule |
|---|---|
| `currentGates(in, handedOver, ageMs)` | `in`, or weather Unknown and not parked when never handed over or older than 5 s |
| `switchesClosed(fitted, s1, s2)` | every fitted switch reads `Closed` |
| `parkHolds(in)` | `parked` AND `switchesClosed` |
| `gateCommand(Open, in)` | `WeatherNotSafe` unless the verdict is `Safe` |
| `gateCommand(Close, in)` | `MountNotParked` unless `parkHolds` |
| `gateCommand(Stop/Abort, in)` | always `None` |
| `interlockLost(status, in)` | state `Closing` AND NOT `parkHolds` |
| `interlockBits(in)` | the `parkInterlock` measured bits above |
| `openMustStop(status, in)` | state `Opening` AND verdict not `Safe` |

## Settings guard

`motionSettingsChanged(a, b)`: any field of `roof` or `park` differs. `settingsSaveRefused(state,
changed)`: changed AND state `Opening` or `Closing`.

## New stored setting

| JSON key | Type | Default | Range | Used for |
|---|---|---|---|---|
| `roof.currentAmpsPerVolt` | number A/V | 8.5 | 0.5-100 | motor amps per volt at the IBT-2's IS output |

| `roof.minRunningAmps` | number A | 0 (off) | 0-20 | with current sense on, below this after 1 s of driving: a broken sense wire |

A config without them loads with 8.5 and 0. Message: `Roof: current scale must be between 0.5 and 100 A/V`.

## Status section: `Snapshot` → `/api/status` `roof`

| Key | Type | Source |
|---|---|---|
| `state` | string | `Roof::stateName` |
| `positionKnown` | boolean | status |
| `positionMm` | number or null | null when not known |
| `positionPercent` | number or null | positionMm / travelMm × 100, one decimal; null when not known |
| `target` | `"open"`, `"closed"` or null | only while opening or closing |
| `driving`, `fromUnknown` | boolean | status |
| `moveElapsedMs`, `moveTimeoutMs` | integer | status |
| `hall` | `{level, counts}` | `Roof::hallLevelName`, raw counts |
| `supplyVolts` | number or null | two decimals; null when unreadable |
| `supplyLow` | boolean | unreadable or below `minSupplyVolts` |
| `minSupplyVolts` | number | setting |
| `currentSense` | boolean | setting |
| `motorAmps` | number or null | two decimals; null when current sense is off or unreadable |
| `maxCurrentAmps` | number | setting |
| `parkSwitches` | `{fitted, switch1, switch2}` | fitted 0-2; each `closed`, `open`, `unreadable`, or `notFitted` |
| `rainContact` | string | `notFitted`, `dry`, `raining`, `unreadable` |
| `closePending` | boolean | a close accepted, waiting for an upload or MQTT connect to finish |
| `fault` | `{kind, cause, measured, limit, text}` or null | null when clear |
| `watchdogTrips` | integer | since start |
| `maxWatchdogBeatAgeMs` | integer | the oldest watchdog heartbeat a step has seen since start (EN needs ≤ 50 ms) |
| `maxStepGapMs` | integer | the longest gap between steps since start |

Text from `describeFault(record)` and `describeRefusal(result)` (contract).

## Firmware: `RoofControl`

Owns the `Roof::Controller`, the inputs, the fault store, the task and the watchdog timer.
State shared between tasks: the controller, the snapshot and the pending settings (mutex); the last
step time, the gate inputs' verdict and park result (mutex, written by the main loop once a second).
