---
description: "RoofRoof wiring: the pin maps of the two boards (ESP32 DevKit, and WT32-ETH01 on the RoofRoof carrier), the 10 kΩ pull-down on the motor driver's enable pin, the voltage dividers, the pin rules and why GPIO34-39 have no pull-ups."
---

# Wiring

This page is the wiring reference for the roof controller: which pin does what on each board, and the passive parts that make the hardware safe before any software runs.

The pins are a setting, chosen from a board preset on the [Pins](../user-guide/pins.md) page or in the [setup wizard](../user-guide/setup-wizard.md): **ESP32 DevKit** (the bench board, on WiFi) or **WT32-ETH01 + RoofRoof carrier v1** (the roof's own controller, on wired Ethernet), or **Custom**, checked by the same [pin rules](#pin-rules). One firmware image serves both boards. The presets and the rules live in `lib/BoardLogic`; `src/MotorDriver.cpp` is the only code that writes the motor driver's pins.

!!! note "What the firmware does with these pins"
    The first thing `setup()` does is read the saved settings, which hold the pins. Straight after, it drives EN low and then RPWM and LPWM low, each level written before its pin becomes an output. Then the roof's control task reads every input below every 20 ms and drives the motor on a person's command ([Roof motion](../user-guide/roof-motion.md#on-the-board)):

    - Hall: the raw 12-bit reading, the median of five per step, compared with the Hall thresholds you set ([Calibration](calibration.md)).
    - Supply: calibrated millivolts × 122 / 22.
    - Current (R_IS, L_IS): read only with current sense on; the larger of the two (eight samples each), × 30 / 20, × the current scale setting, then divided by the PWM duty (the sense output only follows the motor while its side is on).
    - Park switches A and B: internal pull-ups on; a closed contact reads low.
    - RPWM and LPWM: 20 kHz PWM, never both at once; EN: high only while a move is driven.

    With **no board chosen** (a new device), a pin set that breaks the rules, or saved settings that can't be read, it drives **nothing**: every candidate motor pin stays an input, the control task doesn't start and every move is refused ([Pins](../user-guide/pins.md#when-nothing-is-driven)). The EN pull-down then holds the driver off for good.

!!! tip "Trying the firmware without a roof"
    The [bench-mode](../user-guide/bench-mode.md) firmware runs on a bare board with a simulated roof and never drives RPWM, LPWM or EN high. It refuses to simulate while the supply input reads a 12 V supply, so it can't stand in for the roof.

!!! danger "Fit the EN pull-down before connecting the motor driver"
    Until the firmware has read its settings and set the pins, the driver's inputs float. Without the pull-down on EN described [below](#the-en-pull-down) a reset, a crash, a brown-out or flashing can switch the motor on. The WT32 carrier has it on the board; on a DevKit you fit it.

---

## Pin table

The two presets. The names in the second column are the keys of the `pins` setting ([Configuration](../user-guide/configuration.md#pins-and-network)).

### ESP32 DevKit

| GPIO | Setting | Use | Notes |
|---|---|---|---|
| 25 | `rpwm` | IBT-2 (BTS7960) RPWM, forward PWM | Output. Never driven together with LPWM |
| 26 | `lpwm` | IBT-2 LPWM, reverse PWM | Output. Never driven together with RPWM |
| 27 | `enable` | IBT-2 R_EN and L_EN, joined | Output. **10 kΩ pull-down to GND** (required). Low = off |
| 34 | `currentR` | IBT-2 R_IS current sense | Input only, ADC1. Through a 10k/20k divider. Optional: may not be fitted |
| 35 | `currentL` | IBT-2 L_IS current sense | Input only, ADC1. Through a 10k/20k divider. Optional |
| 36 | `supply` | 12 V supply sense | Input only, ADC1. Through a 100k/22k divider |
| 39 | `hall` | SS49E linear Hall sensor output | Input only, ADC1. Needs a divider, see [below](#hall-sensor-ss49e) |
| 32 | `parkA` | Park switch A | Input, internal pull-up. Closed (low) = parked. Optional |
| 33 | `parkB` | Park switch B | Input, internal pull-up. Closed (low) = parked. Optional |

GPIO25, 26 and 27 are not strapping pins, so the pull-down on 27 does not affect boot. This is the map every RoofRoof used before the pins became a setting, and a device upgraded from an earlier release keeps it without being asked.

### WT32-ETH01 + RoofRoof carrier v1

The WT32-ETH01 is a classic ESP32 (4 MB flash, no PSRAM) with a LAN8720 Ethernet PHY. It sits on the RoofRoof carrier, which holds the IBT-2's connector, the dividers and the EN pull-down. The pin names are the WT32's header labels.

| Pin | Setting | Use | Notes |
|---|---|---|---|
| IO14 | `rpwm` | IBT-2 RPWM, forward PWM | Output. Pulses while the chip starts, harmless with EN held low |
| IO15 | `lpwm` | IBT-2 LPWM, reverse PWM | Output. A strapping pin, see [below](#the-wt32-carriers-notes) |
| IO4 | `enable` | IBT-2 R_EN and L_EN, joined | Output. **10 kΩ pull-down to GND on the carrier**. Low = off |
| IO35 | `currentR` | IBT-2 R_IS current sense | Input only, ADC1. Through a 10k/20k divider. Optional |
| IO32 | `currentL` | IBT-2 L_IS current sense | ADC1. Through a 10k/20k divider. Optional |
| IO36 | `supply` | 12 V supply sense | Input only, ADC1. Through a 100k/22k divider |
| IO39 | `hall` | SS49E Hall sensor output | Input only, ADC1. Through its divider |
| IO33 | `parkA` | Park switch A | Input, internal pull-up. Closed (low) = parked. Optional |
| IO5 | `parkB` | Park switch B | Input, internal pull-up. Closed (low) = parked. Optional. A strapping pin, see below |
| IO0, 16, 18, 19, 21, 22, 23, 25, 26, 27 | | Ethernet (LAN8720) | Reserved: never a roof function |

### The WT32 carrier's notes

- **EN on IO4, with the pull-down on the carrier.** IO4 is neither a strapping pin nor one the chip drives while it starts, so nothing at reset can raise it, and the carrier's 10 kΩ holds it low until the firmware drives it. The [wiring check](../user-guide/pins.md#the-wiring-check) confirms the pull-down is there.
- **LPWM on IO15 is a strapping pin.** Held low at reset it only silences the chip's boot messages on the serial port, and IO15 (like IO14 and IO5) pulses while the chip starts. Neither matters for a PWM input: with EN held low by its pull-down the IBT-2 ignores RPWM and LPWM. The Pins page shows this as a warning, not an error.
- **Park B on IO5 is a strapping pin.** It sets the timing of the chip's SDIO interface, which RoofRoof doesn't use, so a park switch closed at reset (a parked mount) changes nothing for a normal start. The Pins page warns about it with that reason.
- **The Ethernet pins.** The LAN8720 uses IO0 (its 50 MHz clock into the ESP32), IO16 (the clock oscillator's power), IO18 (MDIO), IO23 (MDC), IO19, IO21 and IO22 (transmit) and IO25, IO26 and IO27 (receive), with the PHY at address 1. That is why the DevKit's map can't work on a WT32: its motor pins 25 to 27 are the PHY's receive lines. The pin rules refuse any of these pins for a roof function on this board.
- **The header.** The WT32 brings out IO2, 4, 5, 12, 14, 15, 17, 32, 33, 35, 36 and 39, plus IO0, IO1 (TX0) and IO3 (RX0) for flashing ([Flashing](../getting-started/flashing.md#wt32-eth01-the-first-flash)). A Custom map on this board can only use the first list, less IO17, which the pin rules keep free on every board for a WROVER module's PSRAM. IO2 and IO12 are left free by the preset.
- **The analog inputs are all on ADC1** (IO32, 35, 36, 39), as on the DevKit.

### Custom pins

**Custom** on the [Pins](../user-guide/pins.md) page keeps the board and lets you set each function's pin. A Custom map is checked by the same [rules](#pin-rules) as the presets, as you type and again by the device when you save. Every function always has a pin, even a park switch or a current-sense input you haven't fitted: an input pin can't drive anything.

Travel length, speed and the Hall thresholds are settings, never constants in the firmware.

---

## Pin rules

The device refuses a pin set (on the Pins page as you edit, and when it is saved) that puts a function on:

- a GPIO the ESP32 doesn't have (it has 0 to 5, 12 to 19, 21 to 23, 25 to 27, 32 to 36 and 39, besides the flash pins);
- a flash pin (GPIO6 to 11), a PSRAM pin (GPIO16 and 17, on every board: a WROVER module's PSRAM uses them and this build can't detect it), or the serial port used for the log and USB flashing (GPIO1 and 3);
- one of the board's Ethernet pins, or, on the WT32-ETH01, a pin its header doesn't bring out;
- GPIO0, 16, 18 or 23 on any board: a new device drives them at start-up to look for a WT32-ETH01;
- an input-only pin (GPIO34 to 39) for RPWM, LPWM or EN, which must drive;
- a pin off ADC1 (GPIO32 to 39) for the supply, the Hall sensor or a current-sense input: ADC2 can't be read while Wi-Fi is on;
- an input-only pin for a park switch: those pins have no pull-up, so an open switch would float;
- a pin already given to another function;
- any board but the WT32-ETH01, once the device has seen its Ethernet chip answer;
- for EN, a strapping pin or one the chip drives while it starts (GPIO0, 2, 5, 12, 14 and 15): a pulse there could enable the driver while the PWM pins glitch.

It **warns**, and still saves, when any other function sits on a strapping pin (GPIO2, 5, 12 or 15; GPIO0 is already refused as a probe pin), saying what that pin does at reset. The full list, with each warning's meaning, is on the [Pins](../user-guide/pins.md#the-pin-rules) page.

R_EN and L_EN are one EN line by design: the setting has one EN pin.

---

## The motor driver (IBT-2)

The IBT-2 is a BTS7960 H-bridge. The firmware's output rules, enforced in one module (`lib/DriverLogic` decides the levels; `src/MotorDriver.cpp` is the only code that writes the pins) and proved by native tests:

- **EN low means the driver is off.** With EN low the bridge ignores RPWM and LPWM.
- **PWM at 20 kHz**, 8-bit duty, on the one direction's input; the other stays at 0. When the motor starts, the other input is set to 0 first and EN goes high last; when it stops, EN goes low first.
- **A watchdog drops EN** if the control task hasn't stepped for more than 100 ms while the motor is on, without waiting for the task.
- **Both PWM inputs low means stop.**
- **RPWM and LPWM are never both non-zero.** A change of direction goes through stop first.

Join R_EN and L_EN together and connect them to the EN pin: GPIO27 on a DevKit, IO4 on the WT32 carrier.

### The EN pull-down

Fit a **10 kΩ resistor from the EN pin to GND**, close to the IBT-2. On the WT32 carrier it is already on the board.

**Why EN floats until the firmware sets it.** After any reset the ESP32's GPIO pins are inputs with no defined level until software configures them. The ROM bootloader, the second-stage bootloader and the start of the app run first, which takes about 300 ms; then `setup()` reads the saved settings (a few tens of milliseconds, because the pins are among them) and only then drives EN low, then RPWM and LPWM. During that gap the motor pins float, and a floating pin can pick up noise or leakage and read as high to the IBT-2. The same is true during a crash and reset, a brown-out, and while the chip is being flashed. With no board chosen or an invalid pin set the firmware never drives them at all. A hardware pull-down is the only thing that holds EN low at all those times, so the driver stays off whatever the firmware is doing.

**Check it.** Measure it with a meter before the first power-up ([commissioning stage 1](commissioning.md#stage-1-wiring-and-the-en-pull-down-everything-off)), then let the firmware confirm it: the [wiring check](../user-guide/pins.md#the-wiring-check) holds RPWM and LPWM low, reads EN for a moment as an input with the chip's internal pull-up, and fails if EN reads high (the pull-down missing, or weaker than about 20 kΩ).

### Optional pull-downs on RPWM and LPWM

With EN held low by its pull-down the driver ignores RPWM and LPWM, so these are belt and braces: an optional **10 kΩ from RPWM to GND** and the same from **LPWM to GND** keep the PWM inputs low in the same boot gap. They cost nothing and do not affect boot. (On the WT32, IO15 low at reset only silences the boot messages.)

---

## Voltage dividers

The ESP32's inputs must never see more than 3.3 V. Every analog signal that can exceed it goes through a resistor divider, with the lower resistor to GND.

!!! tip "The dividers are settings"
    The values below are the presets' (**DevKit bench wiring**, and **WT32 carrier v1.1**, which also adds back the 0.3 V of the carrier's reverse-polarity diode on the supply sense). If you fit other resistors, or substitute one, enter what is fitted under **Advanced** on the Pins page (analog preset **Custom**): the device then scales the readings with your values and warns when a divider would put more than 3.3 V on its pin ([Advanced: the analog front end](../user-guide/pins.md#advanced-the-analog-front-end)).

### 12 V supply sense

The supply input (GPIO36 on both boards) reads the 12 V supply through a **100 kΩ (top) / 22 kΩ (bottom)** divider. The output is `Vin × 22 / 122`, about 0.18 of the input: 12 V reads about 2.2 V and a 14.4 V charging supply reads about 2.6 V, both under 3.3 V.

### Current sense

R_IS and L_IS are the BTS7960's current-sense outputs. If fitted, each goes through a **10 kΩ (top) / 20 kΩ (bottom)** divider to its input (GPIO34 and GPIO35 on a DevKit, IO35 and IO32 on the WT32 carrier), which scales the signal by two thirds. They are optional: the firmware must work without them.

Under the 20 kHz PWM the sense output is chopped: it follows the motor current only while its side of the bridge is on. Fit a capacitor of about **1 µF across each 20 kΩ lower resistor** (a time constant of about 7 ms) so the ADC reads a steady average; the firmware scales that average back by the duty. Without the capacitor the eight samples per step still average over a few PWM periods, more noisily.

**A broken sense wire.** With the plain divider, a broken R_IS/L_IS wire reads 0 V, which the ADC can't tell from a small running current (it reads nothing below roughly 0.1-0.15 V). Two options:

- set `roof.minRunningAmps` from a bench measurement ([Calibration](calibration.md#current-sense)); or
- fit a **47 kΩ pull-up from the divider's input (the 10 kΩ top) to 3.3 V**. A broken wire then reads about 0.86 V at the pin, roughly 11 A at the default scale: an over-current fault as soon as the motor runs. With the wire intact the pull-up adds a small offset (about 70 µA into the IBT-2's sense resistor); calibrate with it fitted.

### Hall sensor (SS49E)

The SS49E is a linear Hall sensor. It is powered at **5 V**, and its output swings over most of its 5 V supply (roughly 2.5 V at rest with no magnet, towards the supply rails as a magnet approaches). That is above what the ESP32 input tolerates, so **its output must go through a divider before the Hall input** (GPIO39 on both boards). Do not connect it directly.

A 10 kΩ (top) / 20 kΩ (bottom) divider, the same as the current-sense inputs, scales a 5 V swing to under 3.3 V and a resting 2.5 V to about 1.7 V. Check the output range in the sensor's datasheet against your supply and pick a ratio that keeps the highest output under 3.3 V; the firmware's Hall thresholds are settings, so they follow whichever ratio you choose. Keep the divider close to the ESP32 and add a small capacitor (for example 100 nF) from the Hall input to GND to calm noise.

---

## Pull-ups and the input-only pins

**GPIO34, 35, 36 and 39 are input-only, and they have no internal pull-ups or pull-downs** (only GPIO0 to GPIO33 have them). That is fine for the analog inputs, because each one is driven by a divider or a sensor that sets its level. It matters for anything digital: never put a switch on one of these pins and rely on an internal pull-up, because there isn't one. The pin rules refuse a park switch there, unless you fit an external pull-up (about 10 kΩ to 3.3 V) and set that switch's pull-up to **External** under **Advanced** on the Pins page (applied at the next restart).

That is why the park switches are on pins that do have pull-ups: GPIO32 and GPIO33 on a DevKit, IO33 and IO5 on the WT32 carrier. A closed park switch to GND reads low.

Internal pull-ups are weak (tens of kΩ). Over a long cable in a noisy place, such as the run to a roof, add an external pull-up of about 10 kΩ to 3.3 V at the ESP32 end, and put a small capacitor and series resistor on the line if it picks up noise. The firmware turns the internal pull-ups on at start-up.

### Which way round the switches read

The polarity is fixed, not a setting:

- **Park switch**: wire a contact that **closes when the mount is parked**. Closed (low) = parked. A broken wire or an open contact reads "not parked", which is the safe way round: the roof then won't close. The opposite wiring would read a broken wire as parked, so it isn't offered.
- A level counts once it has been read three times in a row (40 ms). Before that, and if the input chatters for more than a second without three equal readings in a row, it reads `unreadable`: not parked.

Which switches are fitted is a setting (`park.switches`: none, one on park A, or both). The live readings are `roof.parkSwitches` in the [status](../api/rest.md#get-apistatus), and the [wiring check](../user-guide/pins.md#the-wiring-check) reads each fitted switch.

Rain isn't wired to the controller: it comes from SQMeter's rain gauge ([Safety inputs](../user-guide/safety-inputs.md#weather)).

---

## Checklist before first power

1. 10 kΩ pull-down from the EN pin to GND, close to the IBT-2 (on the WT32 carrier it is fitted).
2. R_EN and L_EN joined to the EN pin; RPWM and LPWM to theirs (DevKit: 27, 25, 26; WT32 carrier: IO4, IO14, IO15).
3. Optional 10 kΩ pull-downs on RPWM and LPWM.
4. 100k/22k divider on the 12 V supply sense to GPIO36.
5. 10k/20k dividers on the current-sense outputs, if fitted (DevKit: 34 and 35; WT32 carrier: IO35 and IO32).
6. The SS49E output through a divider to GPIO39, not directly.
7. Park switches to GND, if fitted (DevKit: 32 and 33; WT32 carrier: IO33 and IO5).
8. With the motor supply off, power the board and confirm EN, RPWM and LPWM read 0 V (a multimeter on the EN pin) before and after the firmware starts. Then choose the board's preset, restart, and run the [wiring check](../user-guide/pins.md#the-wiring-check).

The whole bring-up, from this checklist to a roof that closes itself, is the [commissioning guide](commissioning.md) ([printable checklist](commissioning-checklist.md)).
