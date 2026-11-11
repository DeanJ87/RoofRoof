# Research: Demo and Bench Mode

## R1. How bench mode is switched on

- **Decision**: A separate build environment (`esp32dev_bench`, `-DROOFROOF_BENCH=1`). Bench code is
  compiled only into that image.
- **Rationale**: A runtime switch (a setting, a REST call, a restore) on a roof controller is one
  mistake away from a roof that reports moves it never made. With a build switch, the standard
  firmware cannot be talked into bench mode at all; the only way in is installing a different
  image, which R2 closes for the remote paths.
- **Alternatives**: a setting with a confirmation (rejected: a restored backup or a wrong click
  reaches it); a jumper on a spare GPIO (rejected: every GPIO is allocated or strapping, and a
  jumper can be left in).

## R2. Keeping a bench image off the roof

- **Decision**: The firmware marker's `build` field is `bench` in a bench image. The standard
  firmware refuses it over the web upload (`Verdict::BenchImage`) and, over command-line OTA,
  checks the marker of the image it just wrote before it is booted (ArduinoOTA calls the end
  handler after `Update.end()` has set the boot partition; the handler scans the new partition and
  puts the boot partition back to the running one if the image would have been refused). A bench
  board accepts standard and bench images.
- **Rationale**: The roof's board is updated over the network once mounted; USB means a person at
  the board with a cable, which is deliberate. `pio run -e esp32dev_bench -t upload --upload-port
  <roof IP>` is the realistic accident, and it goes through command-line OTA.
- **Alternatives**: refusing during the transfer (ArduinoOTA has no data hook); a different
  product name (rejected: "other product" is the wrong message, and a bench board must accept a
  standard image).
- **Note**: The command-line OTA tool reports success (it got "OK" before the end handler runs);
  the board logs the refusal and keeps running. Recorded in the docs.

## R3. A bench image on a powered roof anyway

- **Decision**: The bench build reads the real supply input (GPIO36, the 100k/22k divider) every
  step. 6.0 V or more, or no reading, for 1 s latches an External fault with cause `benchSupply`.
  Before the control task starts, the bench build samples it for 1 s, so the guard has decided
  before any command can arrive. Once tripped it stays tripped for the boot; a clear is re-latched
  at the next step.
- **Rationale**: A bare board has no 12 V (the input reads near 0 V through the divider's 22k, or
  floats). The roof always has it: the 12 V-low threshold depends on it. 6 V is half the nominal
  supply and well above the ADC's floor; a floating input (up to 3.3 V at the pin, ~18 V scaled)
  can trip it, which is safe, and the fix (tie GPIO36 to ground) is documented.
- **Alternatives**: the Hall input (floats on a bare board, reads mid-scale on the roof: not
  decisive); the current inputs (not fitted on the roof yet).

## R4. What reaches the pins in a bench build

- **Decision**: `Driver::physicalOutputs(logical, bench)` returns all-off for a bench build. Every
  write in `MotorDriver` goes through it; in a bench build PWM is never attached (`ledcAttach` is
  not called), so RPWM and LPWM stay plain GPIO outputs held low from `holdOff()`. `MotorDriver`
  keeps the logical state (EN, the duties, the watchdog's trip) and offers it to the simulated roof.
- **Rationale**: One decision, testable natively over every command and duty (constitution I and
  V). The watchdog and trip logic stay real, so bench mode exercises them.

## R5. The simulated roof in lib

- **Decision**: Move `test/roof_sim/RoofSim.h`'s struct into `lib/RoofPlantLogic` unchanged in
  behaviour (fields, defaults, `drive`, `hallCounts`, `motorAmps`, `readings`), add `forSettings()`
  (travel, speed, Hall counts beyond each threshold from the settings, polarity, the open
  direction, current sense) and a supply sag while driving (0 by default, so the existing tests are
  unchanged). `test/roof_sim/RoofSim.h` keeps `RoofTest::RoofSim` as an alias and its `Bench`.
- **Magnet counts**: halfway between the threshold and the rail at each end
  (`(lowCounts + HALL_RAIL_LOW) / 2`, `(highCounts + HALL_RAIL_HIGH) / 2`), and halfway between the
  thresholds in between: never at a rail, always past the hysteresis for the default thresholds.
- **Currents**: running 30% of the over-current limit, stall 150% (so a jam with current sense on
  is an over-current fault, as on the roof).

## R6. Stepping the demo's roof

- **Decision**: The demo core steps the roof in 20 ms of device time, catching up to the clock it
  is given (as `tools/alpaca-sim` does). The close sequence steps every 100 ms, the safety inputs
  every second, the simulated N.I.N.A. poll every 2 s, the SQMeter poll every 5 s, inside the same
  catch-up; the alert engine runs once per tick as before.
- **Rationale**: The motion logic's Hall confirmation and step-gap rules assume 20 ms steps; a
  jump of 10 s at the 10x clock must be 500 steps, not one 10 s step (which would be a LateStep
  fault).

## R7. The demo's simulated SQMeter and N.I.N.A.

- **Decision**: SQMeter answers (on each configured source) `safe` with flags 0, unsafe with bit 0
  (a non-rain reason), rain with bit 9. N.I.N.A. (when an address is configured) answers good
  readings: connected, AtPark, Slewing. A park request slews for 8 s and then reads AtPark, unless
  "park fails" is on (it slews for 8 s and then reads not at park). The panel's "mount parked /
  unparked" is the imaging app parking or unparking the mount itself (same 8 s slew for parking).
  Fitted park switches follow AtPark; a fitted rain contact follows the rain control.
- **Rationale**: Every verdict and park rule then runs on the device's own code with realistic
  inputs; nothing about the outcome is scripted.

## R8. The demo across a reload

- **Decision**: The demo saves the simulated roof's position, its latched fault (as the device's
  fault store would) and the injectors. A reload builds a new controller (a restart): at a magnet
  it reads that end, between them it reads unknown.
- **Rationale**: A reload already restarts the demo device's other state; the roof behaving like
  a restarted board is honest and shows constitution I's boot rule.

## R9. Bench mode's visibility

- **Decision**: `roof.simulation` in the status (`off`, `bench`, `demo`); a banner on every page
  for `bench`; the Dome's description; "Bench: " on alert titles (one place: the dispatcher); a
  warning log at start-up. The demo shows `demo` but no banner (the Demo panel already says it's a
  demo).
- **Alternatives**: a top-level `firmware.build` field (rejected: the roof section is where a
  client looks for the roof, and spec 008's MQTT roof document reuses the roof writer).

## R10. The demo's defaults

- **Decision**: The demo's first-load settings add an SQMeter HTTP source
  (`http://sqmeter.local/api/safety`) and a N.I.N.A. address (`http://192.168.1.20:1888`), so it
  starts safe and parked. The roof settings stay the device's defaults.
