# Feature Specification: Board Presets, Pin Setup and WT32-ETH01 Ethernet

**Feature Branch**: `spec/023-wt32-eth01-board`

**Created**: 2026-11-23

**Status**: Draft

## Context

The controller's pins are fixed in the firmware for an ESP32 DevKit: RPWM 25, LPWM 26, EN 27, the
two current-sense inputs 34 and 35, the supply sense 36, the Hall sensor 39, the park switches 32
and 33, and a rain contact on 4. The roof's own controller will be a different board: a WT32-ETH01,
an ESP32 with a LAN8720 Ethernet PHY on its RMII pins, on a small carrier board that holds the
IBT-2's connector, the dividers and the EN pull-down. Wired Ethernet is wanted because Wi-Fi
dropouts at the roof have been a problem. The WT32-ETH01's Ethernet takes GPIO 0, 16, 18, 19, 21,
22, 23, 25, 26 and 27, so today's pin map cannot work on it.

The bench board stays a DevKit on Wi-Fi, so the firmware must serve both.

This spec makes the pins a setting chosen from board presets (or set by hand), checks every pin set
against the ESP32's electrical rules before it is used, adds a non-moving wiring check to setup,
runs the WT32-ETH01's Ethernet, and removes the local rain contact, because rain now comes from
SQMeter's rain gauge.

## Clarifications

### Session 2026-11-23

Each answer takes the safest option the constitution allows and is also listed under
"Assumptions decided for the owner".

- Q: One firmware image for both boards, or a build per board? → A: One image, flashed the same
  way on either board. The board is a setting read at boot (or, before one is chosen, found by a
  probe); Ethernet starts only when that board has it. The Ethernet driver fits
  the flash budget (research R1), one image keeps one release asset and one update path, and a
  DevKit never starts Ethernet, so its pins are never touched by it.
- Q: What do the presets set? → A: A preset names the board and every pin. "ESP32 DevKit" is
  today's map without the rain contact. "WT32-ETH01 + RoofRoof carrier v1" is the carrier's map.
  "Custom" keeps the board choice and lets each pin be set; it is checked by the same rules.
- Q: Which preset does a device start with? → A: An existing device (settings saved before this
  spec) is migrated to "ESP32 DevKit" explicitly, so it keeps working unchanged. A new device (no
  saved settings) has no board chosen ("unset"): a WT32-ETH01's GPIO 25-27 are its Ethernet's
  receive lines, so the DevKit's motor pins must never be driven on a board nobody has named. With
  no board chosen nothing is driven, every candidate motor pin stays an input, and the setup wizard
  and the Pins page ask for the board.
- Q: How is the board found before it is chosen? → A: At boot, before any pin is touched, the
  device probes for the WT32-ETH01's LAN8720 (oscillator power on GPIO16, MDC 23, MDIO 18, PHY
  address 1, the clock on GPIO0). None of those pins is used by the DevKit's wiring (its motor,
  sense and park pins are 25-27 and 32-39), so the probe can't disturb a DevKit; it is skipped on a
  chip with PSRAM, where GPIO16 is the PSRAM's. If the PHY answers, the device runs Ethernet at
  once (so the wizard can be reached over the cable) and the board question preselects the WT32
  carrier preset; otherwise it suggests the DevKit. The person still confirms the preset: nothing
  is driven until a preset is saved and the device restarts.
- Q: When does a pin change take effect? → A: Only at the next start. A save that changes the
  board, the preset or a pin is refused while the roof moves, a close waits, the close sequence
  works, a calibration run or a jog is under way; otherwise it saves and the page asks for a
  restart. The pins in effect are the ones read at boot until then.
- Q: What happens at boot with a pin set that fails the rules, or saved settings that can't be
  read? → A: Nothing is driven: every candidate motor pin (the saved RPWM, LPWM and EN and every
  preset's) that isn't reserved stays an input with no pull, the motor driver is never armed, the
  roof's control task doesn't start, and the roof shows the fault "Pin setup invalid" with the
  first broken rule (or "No board chosen yet"). The web UI, the network and the settings keep
  working so it can be fixed.
  The fault isn't stored: once a valid set is saved and the device restarts, it is gone.
- Q: Is EN still driven low before anything else? → A: Yes, as early as the pin setup allows:
  the boot reads the saved settings first (the only step before it), then drives EN low and both
  PWM low. Until then the EN pull-down holds the driver off, exactly as through reset, flashing and
  the boot loader today. Constitution I is reworded for this (3.0.0).
- Q: Which pins are refused, and which only warned about? → A: Refused: a pin that doesn't exist;
  one reserved by the flash (6-11), by a WROVER's PSRAM (16, 17, on every board), by UART0 (1, 3) or by
  the board's Ethernet; one the boot probe drives (0, 16, 18, 23) on any board, because a device
  that doesn't know its wiring drives them while it looks for a WT32-ETH01; on the WT32-ETH01, a pin
  its header doesn't bring out; an output (RPWM,
  LPWM, EN) on an input-only pin (34-39); an analog input (both current senses, the supply, the
  Hall) off ADC1 (32-39), since ADC2 can't be read while Wi-Fi runs; a park switch on 34-39 (no
  internal pull-up, so an open switch would float); two functions on one pin; and EN on a pin the
  chip straps or drives during boot (0, 2, 5, 12, 14, 15), since a pulse there could enable the
  driver while the PWM pins glitch. R_EN and L_EN are one EN line by design: the setting has one
  EN pin. Warned (with what each strap does at reset): any other function on 2, 5, 12 or 15 (0 is refused as a probe pin).
- Q: Which wiring checks run? → A: Only checks that can't move the roof, run by a person from the
  setup wizard or the pin page, while the roof is idle, refused while anything else is under way:
  the EN pull-down (EN read for a moment as an input with the internal pull-up, both PWM held low
  so the bridge can't drive even if EN floats high), the Hall reading (in range, not at a rail),
  the supply (plausible volts), the park inputs (each reads open or closed, not chattering), the
  current-sense inputs (near zero while stopped) and Ethernet (on a WT32-ETH01 board: the PHY and
  the link; otherwise a short probe of the PHY's management bus that detects a WT32-ETH01 and
  suggests its preset). Direction and travel need motion and stay with spec 019's attended jog and
  teach, which the check points to.
- Q: How does the wiring check's Ethernet probe stay harmless on a DevKit? → A: It runs only when
  the board in effect has no Ethernet, the chip has no PSRAM, and none of GPIO 0, 16, 18 or 23 is a
  roof function. It powers the WT32's 50 MHz oscillator (GPIO16) for about 15 ms, samples GPIO0 for the
  clock, reads the PHY's two ID registers at address 1 over MDC 23 / MDIO 18 (MDIO is only ever
  pulled low or released), then switches the oscillator off (GPIO16 an input with its pull-down,
  holding it off) and returns MDC and MDIO to inputs.
- Q: How does Ethernet relate to Wi-Fi on a WT32-ETH01? → A: A network mode setting, shown only on
  a board with Ethernet (elsewhere the mode is Wi-Fi):
  - **Auto** (the default): Ethernet starts at boot and is preferred. A saved Wi-Fi network is
    joined once Ethernet has had no address for 30 s (a pulled cable, a dead switch) and stays up
    until the next restart beside Ethernet, which is the default route whenever it has an address.
    Without a saved network nothing else happens.
  - **Ethernet only**: the Wi-Fi radio stays off and the setup hotspot never opens.
  - **Wi-Fi only**: Ethernet isn't started; Wi-Fi works exactly as on a DevKit.
- Q: Are empty Wi-Fi settings an error on a board with Ethernet? → A: No. They are a normal,
  valid state when Ethernet works: the settings, the UI and the status never call them an error
  or "not set up"; the Wi-Fi page says Wi-Fi is optional and shows that the device is on Ethernet.
- Q: When does the setup hotspot open on a board with Ethernet? → A: In Auto, only when no network
  can be reached at boot: no Ethernet address and no working Wi-Fi 45 s after boot, and, when the
  cable has a link, 20 s after the link came up (time for DHCP). A new WT32 on a cable therefore
  never opens it: it is reachable at `roofroof.local` or its DHCP address and the setup wizard runs
  in the browser over Ethernet, with the Wi-Fi step skippable ("You're on Ethernet"). Once any
  network has been joined since boot, a later outage (a pulled cable) falls back to a saved Wi-Fi
  network or keeps retrying Ethernet; it never opens the hotspot, because a passwordless hotspot on
  a running roof controller widens who can reach it, and a power cycle without a network brings
  the hotspot back. In Ethernet only it never opens. In Wi-Fi only the DevKit's rules apply. When
  the hotspot is open and Ethernet or Wi-Fi comes up, the device restarts into normal operation as
  it does today.
- Q: What keys off "the network is up" today, and what does it key off now? → A: Everything that
  checked the Wi-Fi station (mDNS, command-line OTA, NTP's start, the SQMeter and N.I.N.A. poller,
  alert sends, the settings-dependency facts, the platform guard's "network up") now checks "any
  network is up": the Wi-Fi station joined or Ethernet with an address. MQTT, Alpaca discovery and
  the web server already listen on every interface. The status keeps its `wifi` block and gains an
  `ethernet` block.
- Q: How is the rain contact removed? → A: Completely: the `safety.rainContact` setting (an older
  saved value is dropped when the settings load, and never refused), its reading, its status field
  and verdict reason, its wizard question, Settings row, dashboard row, docs and tests. SQMeter's
  rain path (`reasonFlags` bits 9 and 10, its MQTT topic) is untouched. Constitution III loses the
  contact (3.0.0, with the risk stated).
- Q: Where is the pin setup in the UI? → A: A Pins page (from Settings → Roof and the System page)
  with the board, the preset, each function's pin, the rules' errors and warnings as they are
  edited, the pins in effect, and the wiring check. The wizard's Hardware page gains a board
  question (the three presets as choice cards, preselected from the probe), a network question on
  a board with Ethernet (skippable, "You're on Ethernet") and a Check wiring screen, and loses the
  rain question. Settings → Network shows the Ethernet status and the network mode on a board with
  Ethernet.
- Q: How is a WT32-ETH01 flashed? → A: The first time over a 3.3 V USB-serial adapter on TX0, RX0
  and GND with IO0 held to GND at reset (the carrier has a jumper), `pio run -e esp32dev -t upload`
  then `-t uploadfs`; after that, updates go over the network (the Updates page or command-line OTA),
  over Ethernet as over Wi-Fi. The same image serves both boards.

## Constitution rules this spec relies on

| Principle | Rule | Where it is kept |
|---|---|---|
| I | The driver is off whenever not deliberately driven; EN has a hardware pull-down; boot sets EN and both PWM low first | FR-010 to FR-013: the pull-down holds the driver off until the saved pins are read; an invalid set drives nothing |
| I | RPWM and LPWM never driven together; one module sets the outputs | FR-012: `MotorDriver` stays the only writer, now of the pins in effect |
| I | Restarts and settings that change motion are refused while moving | FR-007: a pin change is refused while the roof is busy and applies only at a restart |
| I | Any motion fault locks out motion until cleared | FR-011: an invalid pin set locks out motion (the control task never starts) until it is fixed |
| I, V | The decisions live in `lib/` with native tests | FR-005, FR-016, FR-020, FR-024: the rules, the checks' verdicts, the probe frames and the Wi-Fi plan are pure and tested |
| III | Doubt about the weather resolves to unsafe | FR-030: removing the contact leaves SQMeter's fail-safe path unchanged; constitution 3.0.0 states the risk |
| VI | Request handlers don't block on hardware; flash budget | FR-017: the wiring check runs on the main loop; research R1: the Ethernet driver's size |
| VII | Design system, Stop reachable, i18n | FR-026 to FR-028 |
| VIII | Changing endpoints call `requireAuth` | FR-019: the check and the save need auth |
| IX | Wiring and commissioning guides match the pin table | FR-032 |

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Choose the board's pins from a preset (Priority: P1)

The owner flashes the firmware to the WT32-ETH01 on its carrier, opens the setup wizard over the
setup hotspot, picks "WT32-ETH01 + RoofRoof carrier v1", saves and restarts. The roof's functions
now use the carrier's pins and the motor driver is held off on IO4.

**Why this priority**: without it the roof's real controller can't drive the roof at all.

**Independent Test**: native tests of the presets and rules; Vitest of the page and the wizard
screen against the mock device; on a board, the boot log names the pins in effect.

**Acceptance Scenarios**:

1. **Given** a device with settings saved before this spec, **When** it starts, **Then** it uses
   the DevKit preset (RPWM 25, LPWM 26, EN 27, R_IS 34, L_IS 35, supply 36, Hall 39, park 32/33)
   and nothing else changes.
2. **Given** the DevKit preset in effect, **When** the owner chooses the WT32 carrier preset and
   saves, **Then** the save is accepted, the page says a restart applies it, and after the restart
   the pins in effect are RPWM 14, LPWM 15, EN 4, R_IS 35, L_IS 32, supply 36, Hall 39, park 33/5.
3. **Given** the roof opening, **When** a pin change is saved, **Then** it is refused, naming why.
4. **Given** Custom, **When** the owner sets RPWM to 35, **Then** the page shows the error before
   saving and the device refuses the save if it is sent anyway.
5. **Given** Custom with LPWM on 15, **Then** the page shows a warning that explains GPIO15's
   strap and why it is acceptable for an output while EN is held low; the save is allowed.

---

### User Story 2 - An invalid pin set never drives anything (Priority: P1)

The saved pin set breaks a rule (saved by an older or newer firmware, or the settings can't be read
at all). The device starts with every motor pin as an input, shows "Pin setup invalid" on the roof,
refuses every move, and still serves the web UI so the pins can be fixed.

**Why this priority**: constitution I; a wrong pin could drive a flash line or a PHY line, or
enable the bridge.

**Independent Test**: native tests of the boot decision (valid, invalid, unreadable); firmware
review; bench check with a deliberately invalid set.

**Acceptance Scenarios**:

1. **Given** saved pins with EN on GPIO6, **When** the device starts, **Then** no pin is driven,
   the roof's fault reads "Pin setup invalid: EN on GPIO6: GPIO6 is a flash pin", Open and Close
   are refused with that text, and Settings, the Pins page and Wi-Fi/Ethernet work.
2. **Given** that, **When** a valid set is saved and the device restarts, **Then** the fault is
   gone and the roof starts as usual (unknown away from a magnet).
3. **Given** saved settings that can't be parsed, **When** the device starts, **Then** the defaults
   are loaded for everything else, the pins are not driven, and the fault says the saved pin setup
   couldn't be read.

---

### User Story 3 - Check the wiring during setup without moving the roof (Priority: P1)

On the wizard's Check wiring screen, or on the Pins page, the owner runs the wiring check. Each
check shows its result with the value it read: the EN pull-down, the Hall, the supply, the park
inputs, the current-sense inputs and Ethernet. Direction and travel are pointed to the jog and
teach page.

**Why this priority**: a missing EN pull-down or a swapped pin is the costliest wiring mistake and
is found here before anything moves.

**Independent Test**: native tests of every verdict and boundary; Vitest of the result list; on
the bench, with and without the 10 k pull-down.

**Acceptance Scenarios**:

1. **Given** the 10 k EN pull-down fitted, **When** the check runs, **Then** EN reads low and the
   check passes.
2. **Given** no pull-down, **Then** EN reads high and the check fails, saying the driver would not
   be held off through a reset.
3. **Given** the Hall reading at 4095 (a rail), **Then** the Hall check fails as disconnected.
4. **Given** the supply at 0.3 V, **Then** the supply check fails as not connected; at 12.4 V it
   passes; below the supply limit it warns.
5. **Given** current sense reading 0.1 A while stopped, **Then** it passes; 3 A while stopped warns.
6. **Given** the roof moving, a close waiting, a calibration run or a jog, **When** the check is
   started, **Then** it is refused.
7. **Given** a pin change saved but not yet applied, **When** the check is started, **Then** it is
   refused until the restart, since it would check the old pins.

---

### User Story 4 - Wired Ethernet on the WT32-ETH01 (Priority: P1)

With the WT32 board in effect and a cable in, the device takes an address over Ethernet and
everything works over it: the web UI, mDNS, MQTT and Home Assistant, Alpaca and its discovery,
N.I.N.A. and SQMeter polling, alerts, NTP and command-line OTA. The status shows an `ethernet`
block with the link, speed and address.

**Why this priority**: the reason for the new board.

**Independent Test**: native tests of the Wi-Fi plan; the firmware build; bench check on a real
WT32-ETH01 (pending).

**Acceptance Scenarios**:

1. **Given** the WT32 board, a cable and no Wi-Fi network saved, **When** it starts, **Then** it
   has an address over Ethernet within a few seconds, and no setup hotspot opens.
2. **Given** Auto with a saved network, **When** the cable is unplugged for 30 s, **Then** the
   Wi-Fi station joins; when the cable returns, Ethernet is the default route again.
3. **Given** Auto with no saved network, **When** the cable is unplugged after boot, **Then** no
   hotspot opens and Ethernet is retried; after a restart without a cable, the hotspot opens 45 s
   after boot.
4. **Given** Ethernet only, **Then** the Wi-Fi radio stays off and no hotspot ever opens.
5. **Given** Wi-Fi only, **Then** Ethernet never starts and Wi-Fi behaves as on a DevKit.
6. **Given** the DevKit board, **Then** Ethernet never starts, the mode isn't shown, and Wi-Fi
   behaves exactly as today.
7. **Given** a new WT32-ETH01 on a cable with a link but a slow DHCP server, **Then** the hotspot
   waits 20 s after the link before opening.

---

### User Story 5 - A new board is found, never guessed (Priority: P1)

A WT32-ETH01 freshly flashed has no board chosen. At boot it drives nothing, probes, finds the
LAN8720, runs Ethernet and opens the wizard over the cable with the WT32 carrier preset
preselected. A freshly flashed DevKit finds no PHY and suggests the DevKit preset; it is reached
over the setup hotspot as today.

**Why this priority**: driving GPIO 25-27 on a WT32 fights its Ethernet PHY.

**Acceptance Scenarios**:

1. **Given** a WT32-ETH01 with no saved settings, **When** it starts, **Then** GPIO 25, 26 and 27
   are never driven, Ethernet starts, and the Pins page and the wizard preselect the WT32 carrier
   preset; moves are refused with "No board chosen yet".
2. **Given** a DevKit with no saved settings, **Then** nothing is driven and the DevKit preset is
   suggested.
3. **Given** a device upgraded from the previous release, **Then** it runs the DevKit preset with
   no question asked.

---

### User Story 6 - The wiring check suggests a WT32-ETH01 set up as a DevKit (Priority: P3)

A WT32-ETH01 that was saved with the DevKit preset by mistake: the wiring check's Ethernet probe
finds the LAN8720 at address 1 and suggests the WT32 carrier preset.

**Acceptance Scenarios**:

1. **Given** a WT32-ETH01 on the DevKit preset, **When** the wiring check runs, **Then** the
   Ethernet check reports a LAN8720 PHY found and suggests the WT32 carrier preset.
2. **Given** a DevKit, **Then** it reports no Ethernet PHY found, as expected for that board.
3. **Given** a chip that reports PSRAM, **Then** the probe is skipped, saying why.

---

### User Story 7 - The rain contact is gone (Priority: P2)

Rain comes from SQMeter's rain gauge. The setting, the reading, the status field, the wizard
question, the Settings row, the dashboard row and the docs for the local contact are removed.

**Acceptance Scenarios**:

1. **Given** saved settings with `safety.rainContact: true`, **When** the device starts, **Then**
   the settings load, the value is dropped, and the weather verdict uses SQMeter only.
2. **Given** SQMeter reporting rain (`reasonFlags` bit 9), **Then** the verdict is unsafe as before.
3. **Given** a backup made before this spec, **When** it is restored, **Then** it is accepted and
   the contact's value is dropped.

### Edge Cases

- A preset is chosen and its pin values are sent with different numbers: refused (a named
  preset's pins are fixed); a preset sent alone fills in its pins.
- The WT32 board with a Custom map that puts a function on a pin the WT32 header doesn't bring
  out (e.g. 13): refused.
- A DevKit with PSRAM (WROVER): 16 and 17 are refused; the Ethernet probe is skipped.
- Two functions on one pin, including RPWM = LPWM: refused, naming both.
- A park switch on GPIO0 of a DevKit: refused (a probe pin; a parked mount at power-on would also
  hold the board in its download mode).
- The wiring check started twice: the second start is refused while the first runs.
- Ethernet chosen on a DevKit by mistake (Custom with the WT32 board): Ethernet fails to find a
  PHY; the motor pins of that map aren't wired on a DevKit, and its EN pull-down holds the driver
  off. The status says the PHY wasn't found.
- A restart scheduled after a pin change is held, as every restart, while the roof is busy.
- An old page posts `safety.rainContact`: accepted and ignored.

## Requirements *(mandatory)*

### Functional Requirements

**Pin settings and presets**

- **FR-001**: The settings MUST gain a `pins` group: `preset` (`devkit`, `wt32-carrier-v1`,
  `custom`, `unset`), `board` (`esp32-devkit`, `wt32-eth01`) and one GPIO per function: `rpwm`, `lpwm`,
  `enable`, `currentR`, `currentL`, `supply`, `hall`, `parkA`, `parkB`.
- **FR-002**: A new device (no saved settings) MUST start with `unset`: no board chosen, nothing
  driven (FR-011). Settings saved before this spec MUST load explicitly as the DevKit preset: 25,
  26, 27, 34, 35, 36, 39, 32, 33 on the `esp32-devkit` board. `unset` uses no pin and doesn't stop
  other settings being saved.
- **FR-003**: The WT32 carrier preset MUST be the `wt32-eth01` board with RPWM 14, LPWM 15, EN 4,
  R_IS 35, L_IS 32, supply 36, Hall 39, park A 33, park B 5.
- **FR-004**: A named preset fixes the board and every pin: a save that names a preset without pins
  takes the preset's; a save whose pins differ from its named preset is refused.
- **FR-005**: A pin set MUST be refused when any function's pin: doesn't exist on the ESP32 (only
  0-5, 12-19, 21-23, 25-27, 32-36, 39 do, besides the flash pins; 37 and 38 aren't brought out); is
  one of the boot probe's pins (0, 16, 18, 23) on any board; is a flash pin (6-11); is a PSRAM
  pin (16, 17; reserved on every board, since this build can't detect a WROVER's PSRAM); is UART0 (1, 3); is used by the board's Ethernet (WT32-ETH01:
  0, 16, 18, 19, 21, 22, 23, 25, 26, 27); isn't brought out by the board's header (WT32-ETH01: 2, 4,
  5, 12, 14, 15, 17, 32, 33, 35, 36, 39); is input-only (34-39) for RPWM, LPWM or EN; isn't ADC1
  (32-39) for the current senses, the supply or the Hall; is input-only for a park switch; is shared
  with another function; or, for EN, is 0, 2, 5, 12, 14 or 15. Every broken rule is reported with
  the function, the pin and the reason.
- **FR-006**: A pin set MUST be warned about (and still accepted) for each function other than EN
  on a strapping pin (0, 2, 5, 12, 15), with what that strap does at reset and what it means for
  that function.
- **FR-007**: A save that changes `pins` or `network.mode` MUST be refused while the roof
  moves, a close waits, the close sequence works, a calibration run or a jog is under way. Pins
  apply only at the next start; the settings page MUST say so and offer the restart.
- **FR-008**: The device MUST check a save's pins against its own chip (PSRAM) as well as the
  rules above, and MUST refuse a board other than the WT32-ETH01 on a device whose LAN8720 answered
  (Ethernet started, or the boot probe found it). A backup restore never changes the pins.
- **FR-009**: `GET /api/pins` MUST return the pins in effect, the saved pins, whether a restart is
  needed, whether the pins in effect passed at boot (with their errors and warnings), whether the
  chip has PSRAM, the presets, and each board's Ethernet and header pins. `POST /api/pins/validate`
  MUST return the errors and warnings for a pin set without saving it.

**Boot and safety**

- **FR-010**: At boot the device MUST read its saved settings and then, before anything else that
  touches a pin, set the pins in effect: EN low, then RPWM and LPWM low, each level written before
  its pin becomes an output (as today).
- **FR-011**: If no board is chosen, the saved pin set breaks a rule, or the stored settings exist but can't be read,
  the device MUST drive no pin: every candidate motor pin (the saved RPWM, LPWM and EN and every
  preset's, except reserved ones) set as an input with no pull; the motor driver never attached or
  armed; the roof's control task not started; the roof's fault "Pin setup invalid" with the first
  broken rule (or that no board is chosen, or that the saved setup couldn't be read); every move
  refused with that text. The
  web UI, the network and the settings MUST keep working.
- **FR-012**: `MotorDriver` MUST stay the only writer of RPWM, LPWM and EN, now of the pins in
  effect; none of its rules change.
- **FR-013**: The roof's analog and switch inputs MUST read the pins in effect.
- **FR-014**: The "Pin setup invalid" fault MUST NOT be stored: a valid set and a restart clear it.

**Wiring check**

- **FR-015**: A person MUST be able to run a wiring check from the Pins page and the setup wizard;
  it MUST NOT move the roof or change any setting.
- **FR-016**: The check MUST cover: the EN pull-down (EN as input with pull-up for about 50 µs
  while RPWM and LPWM stay driven low, then back to output low; low passes, high fails); the Hall
  (fails at a rail or outside the setting range, passes otherwise, and says whether it reads a
  magnet); the supply (fails below 3 V as not connected, warns below the supply limit or above
  15 V, passes otherwise); each fitted park switch (open or closed, warns if unreadable or
  chattering, and says what closed means); each current-sense input while stopped (passes below 1
  A, warns above; reported in millivolts when current sense is off); and Ethernet (FR-018). Each
  result has a level (pass, warn, fail, skipped), its reading and a text; the verdicts are pure
  logic with native tests.
- **FR-017**: The check MUST be started with `POST /api/pins/check` (auth) and read with `GET
  /api/pins/check`; it runs on the main loop, not in the request handler. It MUST be refused while
  the roof moves, a close waits, the close sequence works, a calibration run or a jog is under way,
  a fault forbids it (pins invalid), a pin change waits for a restart, or another check runs.
- **FR-018**: Ethernet: on a board with Ethernet, report whether the PHY was found, the link, its
  speed and the address; on a WT32-ETH01 in WiFi only, report it skipped. Otherwise, when the chip has no PSRAM and none of GPIO 0, 16, 18 and 23
  is a roof function, probe for a WT32-ETH01 (oscillator on GPIO16 for about 15 ms, the clock on
  GPIO0, the PHY's ID registers at address 1 over MDC 23 and MDIO 18, then all three back to
  inputs) and suggest the WT32 carrier preset when a LAN8720 answers; otherwise skip, saying why.
- **FR-019**: The check MUST point to spec 019's attended jog and teach for direction and travel.
- **FR-020**: The bench build MUST skip the EN pull-down check (its pins stay low by contract).

**Ethernet**

- **FR-021**: On the `wt32-eth01` board (chosen, or found by the boot probe while none is chosen)
  the device MUST start Ethernet at boot (LAN8720, PHY address 1, MDC 23, MDIO 18, power 16, clock
  in on GPIO0) with the device's host name, and make it the default route while it has an address,
  unless the network mode is Wi-Fi only. On any other board, Ethernet MUST NOT start.
- **FR-022**: A setting `network.mode` (`auto`, `ethernet`, `wifi`; default `auto`) MUST choose
  the network on a board with Ethernet: `auto` joins a saved Wi-Fi network once Ethernet has had no
  address for 30 s and keeps it until restart; `ethernet` keeps the Wi-Fi radio off, refuses Wi-Fi
  scans and joins (409), and never opens the hotspot; `wifi` doesn't start Ethernet. It does
  nothing on a board without Ethernet, and the UI doesn't show it there. Empty Wi-Fi settings are
  valid and never shown as an error on a board with Ethernet.
- **FR-023**: In `auto` the setup hotspot MUST open only when no network has been joined since boot,
  45 s after boot and, with an Ethernet link, 20 s after the link came up; never after a network
  has been joined since boot. Once it is open, Ethernet or Wi-Fi coming up restarts the device as
  joining from the hotspot does today.
- **FR-024**: The Wi-Fi plan (start the station, open the hotspot) MUST be a pure function with
  native tests.
- **FR-034**: With no board chosen, or unreadable settings, the device MUST probe for the
  WT32-ETH01's LAN8720 at boot before touching any pin (skipped with PSRAM), use the board it finds
  for the pin rules' reserved pins and for Ethernet, and report it (`GET /api/pins` `detected`) so
  the Pins page and the wizard preselect the matching preset. It MUST NOT choose or save a preset
  by itself.
- **FR-035**: The docs MUST say how to flash a WT32-ETH01 the first time (3.3 V USB-serial on TX0,
  RX0, GND; IO0 to GND at reset, the carrier's jumper; the pio commands) and that updates then go
  over the network, Ethernet included.
- **FR-025**: mDNS, command-line OTA, NTP, the SQMeter and N.I.N.A. poller, alert sends, the
  settings-dependency facts and the platform guard MUST treat "Wi-Fi joined or Ethernet with an
  address" as the network being up. The status MUST gain an `ethernet` block: `present`, `started`,
  `link`, `ip`, `mac`, `speedMbps`, `fullDuplex`, `phyFound`; the `wifi` block is unchanged.

**UI**

- **FR-026**: A Pins page MUST show the board and preset as choice cards, each function's pin
  (editable only for Custom), the errors and warnings for the edited set, the pins in effect and
  whether a restart is needed, Save and Restart, and the wiring check with its results. It is
  reached from Settings → Roof and the System page.
- **FR-027**: The setup wizard's Hardware page MUST gain a board question (the three presets,
  preselected from the probe; Custom links to the Pins page), on a board with Ethernet a skippable
  network question ("You're on Ethernet", the network mode), and a Check wiring screen, and lose
  the rain contact question; the
  wizard's page indices (`setup.step`) MUST NOT change.
- **FR-028**: Settings → Network MUST show, on a board with Ethernet, the Ethernet status and the
  network mode, and say Wi-Fi is optional; the dashboard's and System page's network rows MUST show Ethernet when it is up. All
  new text through i18n.

**Rain contact removal**

- **FR-029**: `safety.rainContact` MUST be removed from the settings; a saved or restored value MUST
  be dropped without an error.
- **FR-030**: The rain contact's reading, its verdict reason, its status field (`roof.rainContact`
  and the inputs document's `rainContact`), its wizard question, its Settings row, its dashboard row,
  its demo control and its tests MUST be removed. SQMeter's rain and rain-sensor-fault path MUST
  stay as it is.

**Docs and governance**

- **FR-031**: Constitution 3.0.0: Principle I's EN wording for configurable pins and the boot read;
  Principle III without the local contact, with the risk stated; the Platform Constraints' pin
  table as the two presets.
- **FR-032**: Docs: the wiring page with both pin maps and the WT32 carrier's notes, the pin page and
  the wiring check in the user guide, the network page (Ethernet, the Wi-Fi role), the configuration
  reference, the REST reference and OpenAPI, the commissioning guide and checklist, the safety
  inputs page without the contact, the roadmap and the changelog.

### Key Entities

- **Pin map**: the board, the preset and one GPIO per roof function.
- **Board facts**: which GPIOs exist, are reserved (flash, PSRAM, UART0, Ethernet), are on the
  header, are input-only, are ADC1, are straps.
- **Pin report**: every error and warning for a pin map, each with the function, the pin, a code
  and a text.
- **Wiring check**: a list of results (id, level, reading, text) and an optional suggested preset.
- **Network state**: whether Ethernet is present, started, linked and addressed; whether the Wi-Fi
  station has joined; the hotspot.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Every pin rule and warning has a native test that fails if the rule is removed (one
  per rule and per strap).
- **SC-002**: A device upgraded from the previous release keeps its pins and behaviour, with no
  setting to touch (native migration test).
- **SC-003**: With an invalid or unreadable pin set, no GPIO is driven by the firmware's roof code
  (code review against the boot path; bench check).
- **SC-004**: The wiring check finishes within 1 s and never changes a motor output's level except
  EN's mode for under 100 µs (native verdict tests; bench check with a logic analyser).
- **SC-005**: On the WT32-ETH01, every network service works with the Wi-Fi radio off (bench check).
- **SC-006**: The firmware stays below the flash budget's warning line (90 %) with Ethernet linked
  in.

## Assumptions decided for the owner

Each was an open choice; the safest option the constitution allows was taken.

1. **One firmware image** with the board chosen at runtime, not a second build (research R1).
2. **The DevKit preset drops the rain contact** but is otherwise today's map; upgraded devices are
   migrated to it explicitly, new devices start with no board chosen.
3. **A named preset's pins are fixed**; Custom is the only way to change one, and is checked by the
   same rules. Custom keeps the board choice, so reserved Ethernet pins still apply.
4. **Every function always has a pin**, fitted or not: optional park switches and current sense
   still name an input pin (an input pin can't drive anything). No "not wired" value.
5. **EN may not sit on a strap or a pin the chip drives at boot** (0, 2, 5, 12, 14, 15): stricter
   than a warning, because a boot pulse there could enable the bridge.
6. **A park switch on an input-only pin is refused**: those pins have no pull-up, so an open
   switch would float and could read as closed.
7. **Unreadable saved settings lock the pins**, though everything else falls back to defaults as
   before. The defaults have no board chosen, so a later save of any settings keeps "unset" and
   the next restart still drives nothing until a person chooses the board.
8. **The pin fault isn't stored** and can't be cleared by a person: it is a boot-time condition
   that only a valid set and a restart change.
9. **The Ethernet probe briefly powers the WT32's oscillator** (about 15 ms). On a WT32 still on
   the DevKit map, GPIO 25-27 stay driven low against the PHY's receive lines for that time; the
   currents involved are within both parts' limits. It never runs while the roof is busy, with
   PSRAM, or when 0, 16, 18 or 23 is a roof function.
10. **Auto's Wi-Fi fallback starts after 30 s without an Ethernet address and stays until
    restart**, beside Ethernet, rather than switching back and forth.
11. **No hotspot after a network has been joined since boot**, in Auto: a later outage is retried
    (and falls back to a saved Wi-Fi network). A passwordless hotspot on a running roof controller
    widens who can reach it, while a power cycle without a network brings the hotspot back. In
    Ethernet only it never opens, as asked: a board in that mode with no working cable can only be
    reached by switching the mode before the cable is moved, or by a USB flash with the settings
    erased.
12. **The wiring check's limits**: the supply is "not connected" below 3 V and implausible above
    15 V; current sense "near zero" is below 1 A (the ADC's offset at 8.5 A/V is about 0.3 A); the
    Hall reuses the rail limits of the motion logic.
13. **Constitution 3.0.0 (MAJOR)**: removing the rain contact takes a weather source out of
    Principle III, which the governance rule counts as a relaxation. The new risk: rain is seen only
    through SQMeter's gauge; a gauge that misses rain while SQMeter stays fresh and healthy is no
    longer backed by a local contact. A SQMeter outage or a gauge fault still makes the verdict
    unsafe.
14. **Restart codes**: a pin change adds a `pins` reason to the "Restart to apply" note, and
    `network.mode` counts with the Wi-Fi settings.

15. **The boot probe never chooses a preset**: it only preselects one. Nothing is driven until a
    person saves a preset and restarts.
16. **The hotspot waits 20 s after an Ethernet link for DHCP**, on top of the 45 s from boot, so a
    slow DHCP server doesn't open a passwordless hotspot on a cabled device.
17. **The boot probe's pins (0, 16, 18, 23) are refused for every roof function on every board.**
    A device that doesn't know its wiring (new, or with unreadable settings) drives them while it
    looks for a WT32-ETH01, so no map may put EN, a PWM input or a park switch there. None is on the
    WT32 header, and the DevKit preset doesn't use them.
18. **A backup never restores the pins**: they belong to the hardware the device runs on. A
    restore lists different pins as not applied and keeps the device's.

19. **GPIO 16 and 17 are reserved on every board**: the firmware is built without PSRAM support,
    so it can't tell a WROVER (whose PSRAM uses them) from a WROOM. The WT32 carrier doesn't use
    them.
20. **A WT32-ETH01 in WiFi only is probed at boot too**, so a DevKit map can still be refused there.