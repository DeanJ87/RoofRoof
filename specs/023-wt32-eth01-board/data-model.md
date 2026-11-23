# Data Model: Board Presets, Pin Setup and WT32-ETH01 Ethernet

## `Board::PinMap` (lib/BoardLogic, `PinMap.h`; the settings' `pins` group)

| Field | Type | Default (DevKit) | WT32 carrier v1 |
|---|---|---|---|
| `preset` | `Preset`: `Devkit`, `Wt32CarrierV1`, `Custom`, `Unset` | `Devkit` (`Unset` on a new device) | `Wt32CarrierV1` |
| `board` | `BoardId`: `Esp32Devkit`, `Wt32Eth01` | `Esp32Devkit` | `Wt32Eth01` |
| `rpwm` | GPIO (uint8) | 25 | 14 |
| `lpwm` | GPIO | 26 | 15 |
| `enable` | GPIO (R_EN and L_EN joined) | 27 | 4 |
| `currentR` | GPIO (R_IS) | 34 | 35 |
| `currentL` | GPIO (L_IS) | 35 | 32 |
| `supply` | GPIO | 36 | 36 |
| `hall` | GPIO | 39 | 39 |
| `parkA` | GPIO | 32 | 33 |
| `parkB` | GPIO | 33 | 5 |

JSON names: `preset` `devkit` / `wt32-carrier-v1` / `custom`; `board` `esp32-devkit` /
`wt32-eth01`; `unset` for no board chosen (a new device). A missing `pins` group (settings saved before spec 023) loads as the DevKit preset. A named preset fixes `board` and
every pin.

## `Board::Facts`

What the rules need about a board and its chip: the GPIOs that exist, the reserved ones (flash,
PSRAM when present, UART0, the board's Ethernet), the header (WT32: the pins it brings out),
input-only, ADC1, strap and boot-pulse pins. `Board::facts(BoardId, bool psram)`.

## `Board::PinReport`

`std::vector<Issue>` of errors and warnings. `Issue`: `function` (`Function` enum: `Rpwm`, `Lpwm`,
`Enable`, `CurrentR`, `CurrentL`, `Supply`, `Hall`, `ParkA`, `ParkB`), `pin`, `code` (below),
`error` (bool), `other` (the other function, for a shared pin), and `text` (English, the device
catalogue's).

Error codes: `missing` (no such GPIO), `flash`, `psram`, `uart`, `ethernet`, `header`,
`inputOnly` (an output on 34-39), `notAdc1`, `noPullup` (a park switch on 34-39), `shared`,
`enStrap` (EN on 0, 2, 5, 12, 14, 15), `presetMismatch`, `probe` (0, 16, 18, 23 on every board),
`boardDetected` (a board other than the WT32-ETH01 where a LAN8720 answered). Warning codes: `strap0`, `strap2`,
`strap5`, `strap12`, `strap15`.

## `Board::BootPins` (the boot decision)

`valid` (bool), `report`, `reason` (the first error's text, or "the saved pin setup couldn't be
read"), `holdAsInputs` (the candidate motor pins to leave as inputs when not valid).

## Wiring check (`Board::WiringInputs` → `std::vector<Board::Check>`)

Inputs: `enProbe` (`NotRun`, `Low`, `High`), `enSkipped` (why), the Hall counts and the rail
limits and setting range, the supply volts and its limit, each park switch's contact and whether it
is fitted, both current-sense readings (amps or mV) and whether current sense is on, and the
Ethernet result.

`Check`: `id` (`enPulldown`, `hall`, `supply`, `parkA`, `parkB`, `currentR`, `currentL`,
`ethernet`, `motion`), `level` (`Pass`, `Warn`, `Fail`, `Skipped`), `value` (number or none),
`unit`, `text`. The list ends with the `motion` item (always `Skipped`, pointing to the jog and
teach). A `suggestPreset` accompanies an Ethernet probe that found a LAN8720.

## Ethernet probe (`Board::Mdio`)

`readFrame(phy, reg)`: the 46 bits the master drives (32 preamble ones, `01`, `10`, the PHY
address, the register). `decodeRead(bits18, value)`: the turnaround's second bit must be 0 (driven
by the PHY); then 16 data bits, MSB first. `identify(id1, id2)`: `None` (all ones or zeros),
`Lan87xx` (0x0007 / 0xC0Fx), `Other`. `clockSeen(samples)`: both levels present.

## Network plan (`Net::wifiPlan`, lib/BoardLogic `NetworkPlan.h`)

Inputs: `mode` (`Auto`, `Ethernet`, `Wifi`), `hasCredentials`, `ethernetLink`, `ethernetUp`,
`msEthernetDown` (since boot or since it lost its address), `msSinceLink`, `msSinceBoot`,
`joinedSinceBoot`, `stationStarted`, `hotspotOpen`. Outputs: `startStation`, `openHotspot`
(Auto only; Ethernet only never; Wi-Fi only leaves it to WiFiManager's DevKit rules).

## Settings

- `pins` (new group, above).
- `network.mode`: `auto` (default), `ethernet` or `wifi`.
- `safety.rainContact`: removed (ignored when read).

## Status (`GET /api/status`, `/ws/status`)

- New `ethernet`: `present` (the board in effect has Ethernet and runs it), `mode` (the network
  mode in effect: `auto`, `ethernet`, `wifi`), `started`, `phyFound`, `link`,
  `speedMbps`, `fullDuplex`, `ip`, `mac`, `ipv6` (addresses, when on).
- New `pins`: `preset`, `board` (in effect: chosen, or detected while none is), `valid`, `restartNeeded`.
- Removed: `roof.rainContact`.

## Inputs document (`GET /api/roof/inputs`)

Removed: `rainContact`.

## Fault

`RoofIo::FaultCause::PinSetup` (new, after `BenchSupply`): measured = the pin, limit = the function;
never stored. Text: "Pin setup invalid: {reason}".
