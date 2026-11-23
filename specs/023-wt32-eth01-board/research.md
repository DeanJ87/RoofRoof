# Research: Board Presets, Pin Setup and WT32-ETH01 Ethernet

## R1. One image or one build per board

**Decision**: one image (`esp32dev`), the board chosen by the `pins.board` setting at boot.

**Why**:

- The WT32-ETH01 is a classic ESP32 (ESP32-D0WD-V3, 4 MB flash, no PSRAM), the same target,
  partition table and image marker as the DevKit. Nothing in the image needs to differ but which
  pins are used and whether Ethernet starts, and both are runtime decisions.
- The Ethernet driver is already in the framework's ESP-IDF build (`CONFIG_ETH_ENABLED`,
  `CONFIG_ETH_USE_ESP32_EMAC`, the LAN87xx PHY driver in `esp_eth`); linking ESP-IDF's EMAC
  driver and the LAN87xx PHY directly costs about 40 KB (Arduino's `ETH` class linked every PHY it
  supports and measured about 16 KB more, so the firmware uses `esp_eth` itself). With the rest of
  this spec the image is 1,655,312 B, 90.2 % of the slot (plan.md).
- One image keeps one release asset, one OTA path and one image check (spec 011's marker), and
  removes any chance of flashing the "wrong" build: a WT32 running as a DevKit can be switched in
  the settings without a USB cable.
- A separate env (SQMeter's pattern: `-DSQM_BOARD_ESP32S3_ETH`) is needed there because the S3
  boards are different chips with different partition layouts. Here it would add a CI job, a release
  asset and a second budget for no gain.

**Rejected**: a `wt32eth01` PlatformIO env with a compile-time board (SQMeter's `BUILD_BOARD`):
twice the builds, and a WT32 flashed with the DevKit image could never start Ethernet.

**Kept from SQMeter**: the board profile as data (`BoardProfile`: valid GPIOs as a bit mask, ADC1
range, a board's reserved pins), as `lib/BoardLogic`'s `BoardFacts`.

## R2. The ESP32's pin rules

Sources: ESP32 datasheet v4.x §2.2-2.4 (pin list, strapping pins), ESP32 TRM IO_MUX chapter, the
WT32-ETH01 datasheet (pin header), the LAN8720A datasheet.

- GPIOs that exist on the ESP32 package: 0-19, 21-23, 25-27, 32-39. 6-11 are the SPI flash on every
  module; 16-17 are PSRAM on WROVER modules (detected at runtime).
- 34-39 are input only, with no internal pull-up or pull-down.
- ADC1: 32-39. ADC2 (0, 2, 4, 12-15, 25-27) can't be read while Wi-Fi is on.
- UART0: 1 (TX), 3 (RX): the serial log and USB flashing.
- Strapping pins, sampled at reset:
  - GPIO0: low enters the serial download mode (the board doesn't run the firmware).
  - GPIO2: must be low or floating to enter the download mode; ignored for a normal boot.
  - GPIO5: with GPIO15, the SDIO slave's timing; ignored for a normal boot. Outputs a PWM signal
    during boot.
  - GPIO12 (MTDI): the flash voltage; high selects 1.8 V and a 3.3 V flash module then fails to
    start.
  - GPIO15 (MTDO): low silences the boot log; with GPIO5, the SDIO timing. Outputs a PWM signal
    during boot.
  - GPIO14 isn't a strap but also outputs a PWM signal during boot.
- So EN must avoid 0, 2, 5, 12, 14, 15: a pulse at boot there could enable the IBT-2 while its PWM
  inputs glitch. RPWM or LPWM on 14, 15 or 5 is fine: their boot pulses can't drive the bridge
  while EN is held low by its pull-down.
- A park switch closes to ground: on GPIO0 a parked mount at power-on holds the board in the
  download mode (warning); on 2, 12, 15, 5 a low at reset is the default or harmless (warning text
  says so).

**WT32-ETH01** (datasheet v1.x): Ethernet uses GPIO0 (50 MHz RMII clock in), 16 (oscillator
enable), 18 (MDIO), 23 (MDC), 19 (TXD0), 21 (TX_EN), 22 (TXD1), 25 (RXD0), 26 (RXD1), 27
(CRS_DV); the PHY is at address 1 and has no reset GPIO. The header brings out IO2, 4, 5, 12, 14,
15, 17, 32, 33, 35, 36, 39 and IO0/IO1/IO3 for flashing.

## R3. The carrier's pin map

RPWM IO14, LPWM IO15, EN IO4 (10 k pull-down on the carrier), R_IS IO35, L_IS IO32, supply IO36,
Hall IO39, park A IO33, park B IO5. Checked against R2: every output on an output-capable pin;
every analog input on ADC1; EN on a non-strap, quiet pin; the park inputs on pins with pull-ups.
Warnings that remain: LPWM on IO15 (strap, boot pulses, harmless with EN low) and park B on IO5
(strap; a closed switch at reset only changes the SDIO timing, which isn't used).

## R4. Boot order

Today `setup()` starts with `MotorDriver::holdOff()` on fixed pins. Now the pins come from the
settings, so the settings must be read first. Reading and parsing them takes tens of
milliseconds; the boot ROM, the second-stage loader and the Arduino start-up before `setup()`
already take several hundred, during which only the EN pull-down holds the driver off. So reading
the settings first doesn't change what the driver's safety rests on; it adds a few percent to that
window. Constitution I is reworded (3.0.0) to say exactly this. Serial and the logger start before
the read (they use UART0 only).

If the stored settings exist but can't be read, `Config::load` reports it, the defaults are used for
everything else (as today), and the pins are treated as invalid (nothing driven).

## R5. Holding an invalid set safely

Setting a pin as an input with no pull is the state it has after reset, so it can't drive
anything. Doing it to a flash, PSRAM, UART0 or the board's Ethernet pins would break the device,
so those are left alone. The candidates are the saved RPWM, LPWM and EN plus every preset's (25,
26, 27, 14, 15, 4). The control task isn't started and `MotorDriver` is never attached to a pin,
so nothing can drive one later; the external EN pull-down holds the driver off.

## R6. EN pull-down check

With RPWM and LPWM driven low, the IBT-2 can't drive the motor whatever EN does (both half-bridges
low is a brake). EN is switched to an input with the internal pull-up (about 45 kΩ) for 50 µs and
read: with the 10 kΩ pull-down the pin sits near 0.6 V (reads low); without it, it reads high. Then
the low level is written and the pin is an output again. Done under the roof's lock and inside
`MotorDriver`'s EN critical section, so no control step and no watchdog trip can run in between,
and only while the roof is stopped with the driver off.

A pull-down above about 20 kΩ may read high; the failure text says "missing or weaker than about
20 kΩ".

## R7. Ethernet probe on a board set up as a DevKit

The WT32's PHY needs its 50 MHz oscillator to answer on MDIO, and the oscillator is enabled by
GPIO16. The probe: GPIO16 high, 10 ms for the oscillator to start, 64 samples of GPIO0 (a running
clock gives both levels), then two clause-22 MDIO reads at PHY address 1 (registers 2 and 3) by bit
banging MDC (GPIO23) at about 100 kHz and MDIO (GPIO18, only ever driven low or released to its
pull-up); then GPIO16 low and all three back to inputs. The frames and the ID decode are pure
(`lib/BoardLogic/Mdio`). LAN8720A: ID1 0x0007, ID2 0xC0Fx. Any other ID that isn't all ones or all
zeros reports "a PHY answered" without a suggestion.

Guard: only on a board with no Ethernet in effect, no PSRAM (16 is PSRAM there), none of 0, 16,
18, 23 a roof function, the roof idle. On a WT32 still on the DevKit map, GPIO 25-27 are driven
low against the PHY's receive outputs for those ~15 ms; the LAN8720's outputs are current-limited
and the ESP32's pins tolerate it.

## R8. Ethernet with Arduino-ESP32 3.3

`ETH.begin(ETH_PHY_LAN8720, 1, 23, 18, 16, ETH_CLOCK_GPIO0_IN)`; the power pin is used as the PHY's
reset by the driver (low then high), which on the WT32 switches the oscillator on. `ETH.setHostname`
before `begin`; `ETH.enableIPv6(true)` when `wifi.ipv6`; `ETH.setDefault()` once it has an address
so outbound traffic (N.I.N.A., SQMeter, alerts) prefers it over a fallback Wi-Fi. mDNS (ESP-IDF 5)
answers on every default netif, Ethernet included. AsyncTCP, AsyncUDP (Alpaca discovery), PubSubClient
and the HTTP clients use lwIP sockets bound to any interface, so nothing else changes.

## R9. The network mode beside Ethernet

**Decision**: `network.mode`: `auto` (default), `ethernet`, `wifi`.

- Auto joins a saved Wi-Fi network after 30 s without an Ethernet address and keeps it until
  restart, beside Ethernet, which is the default route whenever it has an address. Switching back
  and forth would drop clients that found the device on its Wi-Fi address; two interfaces up is
  ordinary for lwIP.
- Ethernet only keeps the radio off and never opens the hotspot (the owner's choice; Wi-Fi scans and
  joins are refused). Wi-Fi only doesn't start Ethernet.
- **Hotspot** (Auto): only when no network has been joined since boot, 45 s after boot
  (`CaptivePortal::FALLBACK_AFTER_MS`) and, with an Ethernet link, 20 s after the link (DHCP).
  After a join, an outage is retried and never answered with the hotspot: a passwordless hotspot on
  a running roof controller widens who can reach it (spec 010 refuses opening while it is up, but
  every other route is open to it), and a power cycle without a network brings it back. A longer
  timeout was considered and rejected for the same reason.

## R12. A new device's board

A WT32-ETH01's GPIO 25-27 are its PHY's receive outputs; the DevKit's motor map drives them. So a
device with no saved settings has no board chosen and drives nothing. Before any pin is set it
probes (R7's sequence); the DevKit's wiring uses none of GPIO 0, 16, 18, 23, so the probe meets
only unconnected pins there, and with nothing driven yet there is no contention on a WT32 either.
Skipped with PSRAM (GPIO16). A found LAN8720 makes the board in effect the WT32-ETH01 for the pin
rules and Ethernet; the preset is only preselected, never saved by the device. Devices upgraded
from an earlier release have stored settings without `pins` and are migrated to the DevKit
explicitly in the settings reader.

## R10. Wiring check limits

- Hall: the motion logic's rail limits (`Roof::HALL_RAIL_LOW_COUNTS`, `HALL_RAIL_HIGH_COUNTS`) and
  the settings' range (100-3995).
- Supply: below 3 V means no supply or no divider; above 15 V the divider or the scale is wrong;
  below `roof.minSupplyVolts` the motion logic would refuse to move.
- Current sense at rest: the BTS7960's IS offset into the 10k/20k divider reads a few tens of mV;
  with 8.5 A/V that is about 0.3 A. "Near zero" is below 1 A.
- Park inputs: the existing `SwitchInput` debounce gives open, closed or unreadable (chatter).

## R11. Removing the rain contact

The rain contact was optional, off by default, and is now unnecessary: SQMeter's RG-15 gauge
reports rain through `reasonFlags` bit 9 and a gauge fault through bit 10, which the verdict
already treats as unsafe. Removing it frees GPIO4 for the WT32 carrier's EN. A saved `true` is
dropped on load (the JSON reader ignores the key); `Safety::Reason` bit 8 stays unused so the other
bits keep their values in the status and MQTT documents.
