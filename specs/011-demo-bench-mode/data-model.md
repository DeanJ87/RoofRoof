# Data Model: Demo and Bench Mode

## RoofPlant::Plant (lib/RoofPlantLogic)

The simulated roof. Plain values; `drive()` moves it, the rest read it.

| Field | Default | Meaning |
|---|---|---|
| travelMm | 1850 | end stop to end stop |
| speedMmPerSecond | 38 | at 100% duty, scaled by duty/255 |
| positionMm | 0 | 0 closed, travelMm open; clamped to the stops |
| magnetZoneMm | 25 | the Hall sees a magnet this close to an end |
| restingCounts / highMagnetCounts / lowMagnetCounts | 2050 / 3400 / 700 | Hall counts between and at the magnets |
| highEndClosed | false | the high magnet is at the closed end |
| jammed | false | the roof doesn't move; the motor stalls |
| reversedWiring | false | Forward drives toward closed |
| openMagnetMissing / closedMagnetMissing | false | that magnet is never seen |
| railCounts | -1 | >= 0: the Hall reads this whatever the position |
| supplyVolts | 12.6 | at rest |
| sagVolts | 0 | dropped at full duty while driving (forSettings: 0.3) |
| runningAmps / stallAmps | 3 / 18 | current while moving / at a stop or jammed |
| currentFitted | true | false: the current reads NaN |
| lastOutput | Off | the output last driven |

`forSettings(settings, startMm)`: travel and speed from the settings; Hall counts halfway between
each threshold and its rail (`HALL_RAIL_LOW_COUNTS`, `HALL_RAIL_HIGH_COUNTS`), resting halfway
between the thresholds; `highEndClosed` from `hallHighEnd`; `reversedWiring` when the open
direction is Reverse; `currentFitted` from current sense; running 30% and stall 150% of the
current limit; sag 0.3 V.

## RoofPlant::BenchSupplyGuard

| Member | Meaning |
|---|---|
| `LIMIT_VOLTS = 6.0` | a real supply at or above this (or no reading) means a powered roof |
| `CONFIRM_MS = 1000` | for this long without a break |
| `update(volts, nowMs)` | one reading; returns `tripped()` |
| `tripped()` | latched for the boot |
| `measured()` | the reading that tripped it (NaN for none) |

## Driver::physicalOutputs(logical, bench)

`bench` false: `logical` unchanged. `bench` true: `{enable false, rpwm 0, lpwm 0}` whatever
`logical` is.

## FirmwareImage

| | |
|---|---|
| `BUILD_STANDARD = "standard"`, `BUILD_BENCH = "bench"` | the marker's build field |
| `marker(product, layout, build)` | the marker for a build |
| `check(scanner, layout, runningBuild)` | product, layout, then build: same build Ok; a standard image on a bench board Ok; a bench image on a standard board `BenchImage`; anything else `OtherBuild` |
| `Verdict::BenchImage` | "This is a bench-mode image (simulated roof, no motor). Install it over USB on a board that isn't connected to a roof." |

## RoofIo additions

- `FaultCause::BenchSupply` (name `benchSupply`): measured supply volts (NaN: no reading), limit
  6.0 V. Kept in the fault store like the other causes; `describeFault`: "Bench firmware on a
  powered roof: supply 12.4 V >= 6.0 V".
- `Snapshot::simulation` (`"off"`, `"bench"`, `"demo"`), written as `roof.simulation`.

## Alpaca

`domeDeviceInfo(simulation)`: name and UniqueID unchanged; the description is prefixed
"BENCH MODE (simulated roof, no motor): " for bench and "Demo (simulated roof): " for the demo.

## Demo: EmulatedRoof (tools/demo-core)

| Part | |
|---|---|
| controller | `Roof::Controller` with the saved roof settings (new settings taken when no move is active) |
| plant | `RoofPlant::Plant` from `forSettings`, re-set when the settings change (position kept) |
| gates | `RoofIo::GateInputs` from the verdict, the park result and the simulated switches |
| sequence | `CloseSeq::Sequence`, stepped every 100 ms |
| sqmeter | the HTTP and MQTT `Safety::SourceRecord`s, fed a good reply every 5 s when the source is on |
| nina | connected, atPark, slewing, slewEndsMs, parking |
| injectors | unsafe, rain, mountParked, parkFails, jam, supplyLow |
| saved state | positionMm, fault (record), injectors, nina.atPark |

Panel view (`roofPanel()`): the injectors and a one-line summary (state, verdict, park, phase).

Injector JSON (`roofInject`): `{"unsafe":bool,"rain":bool,"mountParked":bool,"parkFails":bool,"jam":bool,"supplyLow":bool}`, any subset.
