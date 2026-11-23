---
description: "The Pins page: choose the board (ESP32 DevKit or WT32-ETH01 on the RoofRoof carrier) or set each pin by hand, the pin rules with their errors and warnings, what happens with no board chosen or an invalid pin set, and the wiring check that tests the EN pull-down, the Hall sensor, the supply, the park switches, current sense and Ethernet without moving the roof."
---

# Pins and the wiring check

The roof's pins are a setting. You choose the board the controller runs on, and with it every pin, from a preset; or you set each pin by hand. The device checks every pin set against the ESP32's electrical rules before it uses it, and drives nothing until a board has been chosen. The **wiring check** then reads the roof's inputs, and the pull-down that holds the motor driver off, without moving anything.

The pin maps themselves, with the parts each board needs, are on [Wiring](../hardware/wiring.md#pin-table).

!!! info "Nothing here moves the roof"
    Neither the Pins page nor the wiring check can open, close or jog the roof. Direction and travel need the roof to move: they are checked attended, with **Jog** and **Teach the ends** on the calibration page ([Teach the ends](calibration-run.md#teach-the-ends)).

---

## The Pins page

The Pins page (**Board and pins**) is at `/pins`, reached from **Settings → Roof** and from the **System** page. It shows:

- the **board** and the **preset**, as choice cards;
- each function's pin (RPWM, LPWM, EN, R_IS, L_IS, supply, Hall, park switch A, park switch B), editable only with **Custom**;
- the errors and warnings for the pins as you edit them, checked by the same rules the device uses;
- the **pins in effect** (the ones read at the last start) and whether a restart is needed to apply the saved ones;
- **Save pins** and **Restart now**;
- the [wiring check](#the-wiring-check) and its results.

### The presets

| Function | ESP32 DevKit | WT32-ETH01 + RoofRoof carrier v1 |
|---|---|---|
| RPWM / LPWM | GPIO25 / GPIO26 | IO14 / IO15 |
| EN (R_EN and L_EN joined, 10 kΩ pull-down) | GPIO27 | IO4 (pull-down on the carrier) |
| R_IS / L_IS current sense | GPIO34 / GPIO35 | IO35 / IO32 |
| 12 V supply sense | GPIO36 | IO36 |
| Hall sensor | GPIO39 | IO39 |
| Park switch A / B | GPIO32 / GPIO33 | IO33 / IO5 |
| Network | WiFi | Ethernet (LAN8720), WiFi optional |

A named preset fixes the board and every pin. **Custom** keeps the board you chose and lets you set each pin; it is checked by the same rules, so a Custom map on the WT32-ETH01 still can't use its Ethernet pins. Every function always has a pin, fitted or not: an unused input pin can't drive anything.

### Saving a change

Pins apply **only at the next start**. A save changes the saved pins; the page then says a restart is needed and offers **Restart now**. Until the restart the device keeps using the pins it read at boot.

- A save that changes the board, the preset or a pin is refused while the roof moves, a close is waiting, the automatic close is under way, or a calibration run or a jog is running: *"The roof is busy: pins and the network's set-up change only while it is still"*. The same holds for the [network mode](network.md#the-network-mode).
- A pin set that breaks a rule is refused with the first error's text. Warnings don't stop a save.
- A named preset sent with pins that differ from it is refused: choose **Custom** to change one.
- A restart, like every restart, waits while the roof is busy.

---

## A new device: no board chosen

A device with no saved settings (new, or erased) has **no board chosen**. It can't guess: the DevKit's motor pins, GPIO25 to 27, are the WT32-ETH01's Ethernet receive lines, so driving them on a board nobody has named could fight the Ethernet chip. So it drives nothing, every move is refused with *"No board chosen yet: choose the board and its pins"*, and the [setup wizard](setup-wizard.md) and the Pins page ask for the board.

To help you answer, the device looks for a WT32-ETH01 at boot, before it touches any pin: it powers the WT32's Ethernet clock (GPIO16) and asks for the LAN8720 Ethernet chip at address 1 over its management lines (MDC on GPIO23, MDIO on GPIO18). A DevKit wired for RoofRoof uses none of those pins, so the probe can't disturb it. On a chip with PSRAM it is skipped, because GPIO16 is the PSRAM's there.

- **A LAN8720 answers**: the Pins page and the wizard preselect **WT32-ETH01 + RoofRoof carrier v1**, and the device starts Ethernet straight away, so you can reach it over the cable ([Network](network.md)).
- **Nothing answers**: they preselect **ESP32 DevKit**, and the device is reached over WiFi as before ([First Boot](../getting-started/first-setup.md)).

The probe only preselects. Nothing is driven until you save a preset and the device restarts.

A device upgraded from a release before the pins were a setting is moved to the **ESP32 DevKit** preset automatically, so it keeps its pins and behaves exactly as before, with no question asked.

---

## When nothing is driven

With no board chosen, a saved pin set that breaks a rule (saved by older or newer firmware, for example), or saved settings that exist but can't be read, the device drives **no pin**:

- every candidate motor pin (the saved RPWM, LPWM and EN, and every preset's) stays an input with no pull, except pins the chip or the board needs;
- the motor driver is never set up, and the roof's control task doesn't start;
- the roof shows the fault `pinSetup`, *"Pin setup invalid: nothing is driven until a valid pin setup is saved and the device restarts"*, and every move is refused with *"Pin setup invalid:"* followed by the reason: the first broken rule (for example *"EN on GPIO6: GPIO6 is a flash pin"*), *"No board chosen yet: choose the board and its pins"*, or *"The saved pin setup couldn't be read"*. **Clear fault** is refused too;
- the web UI, the network and the settings keep working, so you can fix it.

The fault isn't stored and can't be cleared by hand: it is decided at each start. Save a valid pin set and restart, and it is gone; the roof starts as usual (unknown away from a magnet).

When the saved settings can't be read, everything else falls back to its default as before, and the pins to no board chosen: choose the board again.

---

## The pin rules

The same rules run on the Pins page as you type, on the device when you save (also checked against the chip's own PSRAM), and at every start.

### Errors (refused)

| Rule | Text |
|---|---|
| The ESP32 has no such GPIO (it has 0 to 5, 12 to 19, 21 to 23, 25 to 27, 32 to 36 and 39, besides the flash pins) | *"RPWM on GPIO20: the ESP32 has no GPIO20"* |
| A flash pin (GPIO6 to 11) | *"… GPIO6 is a flash pin"* |
| A PSRAM pin (GPIO16, 17) on a chip with PSRAM | *"… GPIO16 is used by this chip's PSRAM"* |
| The serial port (GPIO1, 3) | *"… GPIO1 is the serial port used for the log and USB flashing"* |
| The board's Ethernet (WT32-ETH01: GPIO0, 16, 18, 19, 21, 22, 23, 25, 26, 27) | *"… GPIO25 is used by the WT32-ETH01's Ethernet"* |
| The WT32-ETH01 probe's pins, on every board (GPIO0, 16, 18, 23): a new device drives them at start-up before it knows its wiring | *"… GPIO16 is used to look for a WT32-ETH01 at start-up"* |
| Not on the WT32-ETH01's header (it brings out 2, 4, 5, 12, 14, 15, 17, 32, 33, 35, 36, 39) | *"… the WT32-ETH01's header doesn't bring out GPIO13"* |
| RPWM, LPWM or EN on an input-only pin (34 to 39) | *"RPWM on GPIO35: GPIO35 is input only and can't drive an output"* |
| The supply, the Hall sensor or a current-sense input off ADC1 (32 to 39) | *"… an analog input must be on ADC1 (GPIO32-39), ADC2 can't be read while Wi-Fi is on"* |
| A park switch on an input-only pin (34 to 39) | *"… GPIO34 has no internal pull-up, so an open switch would float"* |
| Two functions on one pin | *"LPWM on GPIO25: GPIO25 is already RPWM"* |
| EN on a strapping pin or one the chip drives while it starts (0, 2, 5, 12, 14, 15) | *"EN on GPIO14: EN can't be on a strapping pin or one the chip drives while it starts (0, 2, 5, 12, 14, 15)"* |
| A named preset with different pins | *"… this preset's pins are fixed; choose Custom to change one"* |

EN is kept off the strapping pins (stricter than a warning) because a pulse there at boot could enable the driver while its PWM inputs glitch. R_EN and L_EN are one EN line: the setting has one EN pin.

### Warnings (saved, with the reason)

Any function other than EN on a strapping pin is allowed, with a warning that says what that pin does at reset:

| Pin | What it does at reset |
|---|---|
| GPIO0 | Refused anyway (the probe's pin). Held low at reset, the board starts in its download mode instead of the firmware |
| GPIO2 | Must be low or floating for USB flashing; a normal start ignores it |
| GPIO5 | Sets the SDIO timing (unused here) and pulses while the chip starts: fine for a switch input, or a PWM input while EN is held low |
| GPIO12 | Sets the flash voltage: held high at reset, a 3.3 V flash module won't start |
| GPIO15 | Low at reset only silences the boot log; it pulses while the chip starts: fine for a switch input, or a PWM input while EN is held low by its pull-down |

The WT32 carrier's preset has two of these warnings, both expected: LPWM on IO15 and park switch B on IO5 ([The WT32 carrier's notes](../hardware/wiring.md#the-wt32-carriers-notes)).

---

## The wiring check

Run it from the Pins page, or from the **Check wiring** screen of the [setup wizard](setup-wizard.md)'s hardware part. Do it at the first power-up, with the motor's leads disconnected ([commissioning stage 2](../hardware/commissioning.md#stage-2-first-power-up-motor-still-disconnected)), and again whenever you change the wiring or the pins. It takes under a second, never moves the roof and changes no setting.

It is refused (`409` with the reason) while the roof moves, a close is waiting, the automatic close is under way, or a calibration run or a jog is running; while the pins are invalid or no board is chosen; while a saved pin change waits for a restart (it would check the old pins: *"Restart first: the saved pins aren't in effect yet"*); and while another check runs.

Each result is **Pass**, **Check** (a warning; `warn` in the API), **Fail** or **Skipped**, with the value it read:

| Check | What it does | Results |
|---|---|---|
| **EN pull-down** | Holds RPWM and LPWM low (so the bridge can't drive whatever EN does), switches EN for about 50 µs to an input with the chip's internal pull-up, reads it, and drives it low again | **Pass**: EN reads low, so its pull-down holds the driver off. **Fail**: EN reads high, so the pull-down is missing or weaker than about 20 kΩ, and a reset or a crash wouldn't hold the driver off. **Skipped** on the bench build, which keeps its motor pins low |
| **Hall** | The raw reading | **Fail** at a rail (the sensor or its divider disconnected or shorted); **Check** outside the thresholds' range (100 to 3995); **Pass** otherwise |
| **Supply** | The 12 V supply sense | **Fail** below 3 V (no supply, or its divider isn't connected) or unreadable; **Check** above 15 V (check the 100k/22k divider) or below `roof.minSupplyVolts` (the roof won't move); **Pass** otherwise |
| **Park switch A**, **B** | Each fitted switch | **Pass** open (not parked) or closed (parked), saying which; **Check** unreadable (the input changes too often: check the switch and its wire); **Skipped** when not fitted (`park.switches`) |
| **Current sense R**, **L** | Each input while the roof is stopped | With current sense on: **Pass** below 1 A, **Check** above (check the IS wire and its divider). With it off, in millivolts at the pin: **Pass** below 80 mV, **Check** above |
| **Ethernet** | See below | |
| **Direction and travel** | Always **Skipped** | They need the roof to move: use **Jog** and **Teach the ends** on the calibration page |

### The Ethernet check

- **On a board running Ethernet** (the WT32-ETH01): **Fail** if the LAN8720 didn't answer and Ethernet didn't start; **Check** with no link (check the cable) or a link without an address yet; **Pass** with a link and an address, with its speed.
- **On a board set up as a DevKit**: a short probe looks for a WT32-ETH01, the same as the one at boot: the Ethernet clock on GPIO16 for about 15 ms, the clock seen on GPIO0, the PHY's ID read at address 1 over MDC (GPIO23) and MDIO (GPIO18, only ever pulled low or released), then all three back to inputs. No Ethernet PHY is the expected **Pass** on a DevKit. A LAN8720 answering is a **Check**: *"this looks like a WT32-ETH01, so choose its preset"*, and the page offers **Use that preset**. Another PHY, or a clock with no PHY, is a **Check** too.
- The probe is **Skipped**, saying why, on a chip with PSRAM (GPIO16 is the PSRAM's). The pin rules keep every roof function off GPIO0, 16, 18 and 23, so the probe never meets one.

On a WT32-ETH01 still set up as a DevKit, the DevKit's motor pins (GPIO25 to 27) are driven low against the Ethernet chip's receive lines for those 15 ms; both parts tolerate it. Choose the right preset and restart to end it.

---

## From a script

The page uses four routes ([REST API](../api/rest.md#pins)):

```bash
curl http://roofroof.local/api/pins                      # pins in effect, saved, presets, boards
curl -X POST http://roofroof.local/api/pins/validate \
  -H "Content-Type: application/json" -d '{"preset": "custom", "board": "esp32-devkit", "rpwm": 35}'
curl -u admin:pass -X POST http://roofroof.local/api/pins/check   # start the wiring check
curl http://roofroof.local/api/pins/check                # its results
```

The pins are saved with the other settings, as `pins` in [`POST /api/config`](../api/rest.md#post-apiconfig) ([Configuration](configuration.md#pins-and-network)). The status says which preset is in effect and whether a restart is needed (`pins` in [`GET /api/status`](../api/rest.md#get-apistatus)).
