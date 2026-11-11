# Data Model: Roof State and Motion Logic

All types live in `namespace SQM::Roof` (`lib/RoofLogic`) except the stored settings
(`SQM::RoofConfig`, `lib/ConfigModel`).

## Stored settings: `RoofConfig` (config JSON `roof`)

| JSON key | Type | Default | Range / rule | Logic field |
|---|---|---|---|---|
| `travelMm` | integer mm | 1850 | 100-20000 | `travelMm` |
| `speedMmPerSecond` | number mm/s | 38 | 1-500 | `speedMmPerSecond` |
| `timeoutMarginSeconds` | integer s | 15 | 3-300 | `timeoutMarginMs` (×1000) |
| `leaveMagnetSeconds` | integer s | 10 | 2-120 | `leaveMagnetMs` (×1000) |
| `rampUpMs` | integer ms | 2000 | 0-30000 | `rampUpMs` |
| `rampDownMs` | integer ms | 3000 | 0-30000 | `rampDownMs` |
| `topDutyPercent` | integer % | 100 | 20-100 | `topDutyPercent` |
| `slowDutyPercent` | integer % | 40 | 10-100, and ≤ `topDutyPercent` | `slowDutyPercent` |
| `openDirection` | `"forward"` \| `"reverse"` | `"forward"` | either | `openDirection` (`Driver::Command`) |
| `hallHighEnd` | `"open"` \| `"closed"` | `"open"` | either | `hallHighEnd` (`End`) |
| `hallHighCounts` | integer counts | 3000 | 100-3995, and ≥ `hallLowCounts` + 80 | `hallHighCounts` |
| `hallLowCounts` | integer counts | 1100 | 100-3995 | `hallLowCounts` |
| `currentSenseEnabled` | boolean | false | - | `currentSenseEnabled` |
| `maxCurrentAmps` | number A | 10 | 0.5-40 | `maxCurrentAmps` |
| `minSupplyVolts` | number V | 11.0 | 6-15 | `minSupplyVolts` |

A config without `roof` (any earlier release) loads with these defaults (current sense off). Cross
rules have one message each, shared by the device and the web UI:
- `Roof: slow duty can't be above the top duty`
- `Roof: the Hall high threshold must be at least 80 counts above the low threshold`

## Logic settings: `Roof::Settings`

The fields above in the logic's units, plus `validSettings(settings)` (the same rules; the logic
refuses every move with `settingsInvalid` while its settings are invalid, so a bad value can never
divide by zero or overflow). `Roof::settingsFromConfig(const RoofConfig &)` converts.

Fixed values (`RoofSettings.h`): `DEAD_TIME_MS = 500`, `HALL_CONFIRM_READINGS = 3`,
`HALL_HYSTERESIS_COUNTS = 40`, `HALL_RAIL_LOW_COUNTS = 30`, `HALL_RAIL_HIGH_COUNTS = 4065`,
`CONFIRM_MS = 250`, `MAX_STEP_GAP_MS = 100`.

## Inputs: `Readings`

| Field | Type | Meaning |
|---|---|---|
| `nowMs` | uint32 | monotonic milliseconds (wraps) |
| `hallCounts` | uint16 | raw ADC counts 0-4095 |
| `supplyVolts` | float | supply voltage; NaN when unreadable |
| `motorAmps` | float | motor current; NaN when not fitted or unreadable |

## Output: `Output`

`command` (`Driver::Command`: Off, Forward, Reverse) and `duty` (0-255), handed to
`Driver::outputsFor`. Off whenever no move is being driven.

## Enumerations (machine names in brackets, for status documents)

- `State`: Unknown (`unknown`), Closed (`closed`), Open (`open`), Opening (`opening`), Closing
  (`closing`), Stopped (`stopped`), Error (`error`).
- `End`: Closed (`closed`), Open (`open`).
- `Command`: Open, Close, Stop, Abort. `Origin`: Person, Automatic.
- `HallLevel`: Unsettled (`unsettled`), Low (`low`), Between (`between`), High (`high`), Rail (`rail`).
- `FaultKind` (measured / limit): None (`none`), Timeout (`timeout`; elapsed ms / timeout ms),
  DidNotLeave (`didNotLeave`; elapsed ms / leave ms), WrongMagnet (`wrongMagnet`; the end read as
  0 closed or 1 open / the target), HallRail (`hallRail`; counts / the rail it passed),
  OverCurrent (`overCurrent`; A / limit A), CurrentUnreadable (`currentUnreadable`; NaN / limit A),
  SupplyLow (`supplyLow`; V or NaN / minimum V), LateStep (`lateStep`; gap ms / 100 ms), External
  (`external`; caller's values).
- `Refusal` (measured / limit): None, FaultLatched (`faultLatched`), HallNotSettled
  (`hallNotSettled`), PositionUnknown (`positionUnknown`), SupplyLow
  (`supplyLow`; V / minimum), SupplyUnreadable (`supplyUnreadable`), CurrentUnreadable
  (`currentUnreadable`), SettingsInvalid (`settingsInvalid`), NotAPerson (`notAPerson`, for
  clearing a fault), Moving (`moving`, for new settings).

## Status: `Status`

| Field | Meaning |
|---|---|
| `state` | the State above |
| `positionKnown`, `positionMm` | 0 = closed, `travelMm` = open; `positionMm` meaningless when not known |
| `target` | the end being moved to (valid while opening/closing) |
| `driving` | the driver is on (false during the dead time) |
| `fromUnknown` | the move started from an unknown position (slow, person only) |
| `moveElapsedMs`, `moveTimeoutMs` | time since the driver turned on, and the move's limit |
| `fault` | the latched fault (kind None when clear) |
| `hall`, `hallCounts` | the confirmed Hall level and the last raw reading |

## State transitions

```text
unknown ──person open/close──▶ opening/closing (slow, fromUnknown)
closed  ──open──▶ opening ──open magnet──▶ open
open    ──close─▶ closing ──closed magnet─▶ closed
opening/closing ──stop──▶ stopped (estimate kept) │ ──abort──▶ unknown (or the end at a magnet)
opening/closing ──other direction──▶ the other move, after the dead time
stopped ──open/close──▶ opening/closing (from the estimate)
closed/open (idle) ──reading leaves the magnet──▶ unknown
unknown/stopped (idle) ──a magnet read──▶ closed/open
any ──fault──▶ error ──person clears──▶ closed/open at a magnet, else unknown
```
