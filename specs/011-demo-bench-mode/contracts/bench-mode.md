# Contract: Bench mode

## Build

- Environment `esp32dev_bench` in `platformio.ini`: `extends = env:esp32dev`, adds
  `-DROOFROOF_BENCH=1`. Not a default environment. Not a release asset.
- `include/BenchMode.h`: `SQM::Bench::BUILD` (`true` only in the bench build) and
  `SQM::Bench::BUILD_NAME` (`"standard"` / `"bench"`).

## Pins

- `MotorDriver::holdOff()` as before (EN, RPWM, LPWM outputs, low).
- Bench: `MotorDriver::begin()` attaches no PWM. Every level written goes through
  `Driver::physicalOutputs(logical, Bench::BUILD)`. GPIO25/26/27 never go high.
- `MotorDriver::logicalOutputs()` (bench only): what the driver would apply (EN as decided with the
  trip, arming and heartbeat rules; the duties).

## Start

The simulated roof starts half-way after every restart: unknown, the Dome reports an error,
until a person moves it.

## Readings

- Bench: each control step, under the roof's lock, the plant is driven by
  `MotorDriver::logicalOutputs()` for the step's elapsed time (at most 100 ms), then its Hall,
  supply and current replace the ADC's. The real supply reading goes to the guard first.
- The park switch and rain inputs are read from their pins as on the standard build.

## Guard

- `RoofControl::begin()` (bench): about 1 s of real supply readings, 20 ms apart, before the task
  starts. Then every step.
- Tripped: `raiseFault(FaultCause::BenchSupply, measured, 6.0)`, every step for the rest of the
  boot. A clear-fault on a tripped board answers 409 with reason `benchSupply` and the fault text.

## Upload rules

| Running \ image | standard | bench |
|---|---|---|
| standard | Ok | refused: `BenchImage` (web upload: 400 with the message, before `Update.end()`; command-line OTA: the running firmware is made the boot partition again before the check, the image is checked over its own length, and a refused image's first sector is erased so no rollback can boot it; logged) |
| bench | Ok | Ok |

## Visible

| Where | Standard | Bench |
|---|---|---|
| `/api/status`, `/ws/status`, MQTT `<base>/state` `roof.simulation` | `"off"` | `"bench"` |
| Home Assistant device `sw_version` | the version | the version + " (bench)" |
| Web UI | - | a warning banner on every page |
| Alpaca Dome Description | "Roll-off roof: shutter open, close and stop only" | "BENCH MODE (simulated roof, no motor): Roll-off roof: ..." |
| Alert titles | unchanged | "Bench: " + title |
| Log at start-up | - | warning: bench mode, simulated roof, motor never driven |
