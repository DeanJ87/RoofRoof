---
description: "RoofRoof wiring: the ESP32 pin table, the 10 kΩ pull-down on the motor driver's enable pin, the voltage dividers and why GPIO34-39 have no pull-ups."
---

# Wiring

This page is the wiring reference for the roof controller's ESP32: which pin does what, and the passive parts that make the hardware safe before any software runs. The pins are fixed by the hardware design (the project's constitution), not settings, and `include/RoofPins.h` is the one place in the firmware that names them.

!!! note "What the firmware does with these pins"
    As the first statement of `setup()` it sets GPIO27 (EN), GPIO25 (RPWM) and GPIO26 (LPWM) as outputs, all low. Then the roof's control task reads every input below every 20 ms and drives the motor on a person's command ([Roof motion](../user-guide/roof-motion.md#on-the-board)):

    - GPIO39 (Hall): the raw 12-bit reading, the median of five per step, compared with the Hall thresholds you set ([Calibration](calibration.md)).
    - GPIO36 (supply): calibrated millivolts × 122 / 22.
    - GPIO34 and GPIO35 (current): read only with current sense on; the larger of the two (eight samples each), × 30 / 20, × the current scale setting, then divided by the PWM duty (the sense output only follows the motor while its side is on).
    - GPIO32, GPIO33 (park switches) and GPIO4 (rain contact): internal pull-ups on; a closed contact reads low.
    - GPIO25 and GPIO26: 20 kHz PWM, never both at once; GPIO27: high only while a move is driven.

!!! tip "Trying the firmware without a roof"
    The [bench-mode](../user-guide/bench-mode.md) firmware runs on a bare board with a simulated roof and never drives GPIO25, 26 or 27. It refuses to simulate while GPIO36 reads a 12 V supply, so it can't stand in for the roof.

!!! danger "Fit the EN pull-down before connecting the motor driver"
    Until the firmware starts, the driver's inputs float. Without the pull-down on EN described [below](#the-en-pull-down) a reset, a crash, a brown-out or flashing can switch the motor on.

---

## Pin table

| GPIO | Name in firmware | Use | Notes |
|---|---|---|---|
| 25 | `RPWM` | IBT-2 (BTS7960) forward PWM | Output. Never driven together with LPWM |
| 26 | `LPWM` | IBT-2 reverse PWM | Output. Never driven together with RPWM |
| 27 | `ENABLE` | IBT-2 R_EN and L_EN, joined | Output. **10 kΩ pull-down to GND** (required). Low = off |
| 34 | `CURRENT_R` | IBT-2 R_IS current sense | Input only, ADC1. Through a 10k/20k divider. Optional: may not be fitted |
| 35 | `CURRENT_L` | IBT-2 L_IS current sense | Input only, ADC1. Through a 10k/20k divider. Optional |
| 36 | `SUPPLY_SENSE` | 12 V supply sense | Input only, ADC1. Through a 100k/22k divider |
| 39 | `HALL_SENSOR` | SS49E linear Hall sensor output | Input only, ADC1. Needs a divider, see [below](#hall-sensor-ss49e) |
| 32 | `PARK_A` | Park switch 1 | Input, internal pull-up. Closed (low) = parked. Optional |
| 33 | `PARK_B` | Park switch 2 | Input, internal pull-up. Closed (low) = parked. Optional |
| 4 | `RAIN` | Rain contact | Input, internal pull-up. Closed (low) = raining. Optional |

All analog inputs are on ADC1, so they keep working while WiFi is on (ADC2 does not). GPIO25, 26 and 27 are not strapping pins, so the pull-down on 27 does not affect boot.

Travel length, speed and the Hall thresholds are settings, never constants in the firmware.

---

## The motor driver (IBT-2)

The IBT-2 is a BTS7960 H-bridge. The firmware's output rules, enforced in one module (`lib/DriverLogic` decides the levels; `src/MotorDriver.cpp` is the only code that writes the pins) and proved by native tests:

- **EN low means the driver is off.** With EN low the bridge ignores RPWM and LPWM.
- **PWM at 20 kHz**, 8-bit duty, on the one direction's input; the other stays at 0. When the motor starts, the other input is set to 0 first and EN goes high last; when it stops, EN goes low first.
- **A watchdog drops EN** if the control task hasn't stepped for more than 100 ms while the motor is on, without waiting for the task.
- **Both PWM inputs low means stop.**
- **RPWM and LPWM are never both non-zero.** A change of direction goes through stop first.

Join R_EN and L_EN together and connect them to GPIO27.

### The EN pull-down

Fit a **10 kΩ resistor from GPIO27 (EN) to GND**, close to the IBT-2.

**Why EN floats until `setup()` runs.** After any reset the ESP32's GPIO pins are inputs with no defined level until software configures them. The ROM bootloader, the second-stage bootloader and the start of the app run first, which takes about 300 ms, and only then does `setup()` run and `MotorDriver::holdOff()` drive the pins low (it is the very first statement, before the serial port starts). During that gap GPIO25, 26 and 27 float, and a floating pin can pick up noise or leakage and read as high to the IBT-2. The same is true during a crash and reset, a brown-out, and while the chip is being flashed. A hardware pull-down is the only thing that holds EN low at those times, so the driver stays off whatever the firmware is doing.

### Optional pull-downs on RPWM and LPWM

With EN held low by its pull-down the driver ignores RPWM and LPWM, so these are belt and braces: an optional **10 kΩ from GPIO25 (RPWM) to GND** and the same from **GPIO26 (LPWM) to GND** keep the PWM inputs low in the same boot gap. They cost nothing and do not affect boot.

---

## Voltage dividers

The ESP32's inputs must never see more than 3.3 V. Every analog signal that can exceed it goes through a resistor divider, with the lower resistor to GND.

### 12 V supply sense (GPIO36)

`SUPPLY_SENSE` reads the 12 V supply through a **100 kΩ (top) / 22 kΩ (bottom)** divider. The output is `Vin × 22 / 122`, about 0.18 of the input: 12 V reads about 2.2 V and a 14.4 V charging supply reads about 2.6 V, both under 3.3 V.

### Current sense (GPIO34, GPIO35)

R_IS and L_IS are the BTS7960's current-sense outputs. If fitted, each goes through a **10 kΩ (top) / 20 kΩ (bottom)** divider to GPIO34 and GPIO35, which scales the signal by two thirds. They are optional: the firmware must work without them.

Under the 20 kHz PWM the sense output is chopped: it follows the motor current only while its side of the bridge is on. Fit a capacitor of about **1 µF across each 20 kΩ lower resistor** (a time constant of about 7 ms) so the ADC reads a steady average; the firmware scales that average back by the duty. Without the capacitor the eight samples per step still average over a few PWM periods, more noisily.

**A broken sense wire.** With the plain divider, a broken R_IS/L_IS wire reads 0 V, which the ADC can't tell from a small running current (it reads nothing below roughly 0.1-0.15 V). Two options:

- set `roof.minRunningAmps` from a bench measurement ([Calibration](calibration.md#current-sense)); or
- fit a **47 kΩ pull-up from the divider's input (the 10 kΩ top) to 3.3 V**. A broken wire then reads about 0.86 V at the pin, roughly 11 A at the default scale: an over-current fault as soon as the motor runs. With the wire intact the pull-up adds a small offset (about 70 µA into the IBT-2's sense resistor); calibrate with it fitted.

### Hall sensor (SS49E)

The SS49E is a linear Hall sensor. It is powered at **5 V**, and its output swings over most of its 5 V supply (roughly 2.5 V at rest with no magnet, towards the supply rails as a magnet approaches). That is above what the ESP32 input tolerates, so **its output must go through a divider before GPIO39**. Do not connect it directly.

A 10 kΩ (top) / 20 kΩ (bottom) divider, the same as the current-sense inputs, scales a 5 V swing to under 3.3 V and a resting 2.5 V to about 1.7 V. Check the output range in the sensor's datasheet against your supply and pick a ratio that keeps the highest output under 3.3 V; the firmware's Hall thresholds are settings, so they follow whichever ratio you choose. Keep the divider close to the ESP32 and add a small capacitor (for example 100 nF) from GPIO39 to GND to calm noise.

---

## Pull-ups and the input-only pins

**GPIO34, 35, 36 and 39 are input-only, and they have no internal pull-ups or pull-downs** (only GPIO0 to GPIO33 have them). That is fine for the four analog inputs above, because each one is driven by a divider or a sensor that sets its level. It matters for anything digital: never put a switch on one of these pins and rely on an internal pull-up, because there isn't one.

That is why the digital inputs are on pins that do have pull-ups:

- **PARK_A and PARK_B on GPIO32 and GPIO33** are bidirectional pins with internal pull-ups. A closed park switch to GND reads low.
- **RAIN on GPIO4** is the same: a rain contact to GND, internal pull-up.

Internal pull-ups are weak (tens of kΩ). Over a long cable in a noisy place, such as the run to a roof, add an external pull-up of about 10 kΩ to 3.3 V at the ESP32 end, and put a small capacitor and series resistor on the line if it picks up noise. The firmware turns the internal pull-ups on at start-up.

### Which way round the switches read

The polarity is fixed, not a setting:

- **Park switch**: wire a contact that **closes when the mount is parked**. Closed (low) = parked. A broken wire or an open contact reads "not parked", which is the safe way round: the roof then won't close. The opposite wiring would read a broken wire as parked, so it isn't offered.
- **Rain contact**: closed (low) = raining. A broken wire reads dry; the rain contact only ever adds to SQMeter's weather verdict, which stays the weather source.
- A level counts once it has been read three times in a row (40 ms). Before that, and if the input chatters for more than a second without three equal readings in a row, it reads `unreadable`: not parked, and unsafe.

Which switches are fitted is a setting (`park.switches`: none, one on GPIO32, or two), and so is whether the rain contact is fitted (`safety.rainContact`). The live readings are `roof.parkSwitches` and `roof.rainContact` in the [status](../api/rest.md#get-apistatus).

---

## Checklist before first power

1. 10 kΩ pull-down from GPIO27 to GND, close to the IBT-2.
2. R_EN and L_EN joined to GPIO27; RPWM to GPIO25; LPWM to GPIO26.
3. Optional 10 kΩ pull-downs on GPIO25 and GPIO26.
4. 100k/22k divider on the 12 V supply sense to GPIO36.
5. 10k/20k dividers on the current-sense outputs to GPIO34 and GPIO35, if fitted.
6. The SS49E output through a divider to GPIO39, not directly.
7. Park switches to GND on GPIO32 and GPIO33; rain contact to GND on GPIO4, if fitted.
8. With the motor supply off, power the ESP32 and confirm EN, RPWM and LPWM read 0 V (a multimeter on GPIO27) before and after the firmware starts.

The whole bring-up, from this checklist to a roof that closes itself, is the [commissioning guide](commissioning.md) ([printable checklist](commissioning-checklist.md)).
