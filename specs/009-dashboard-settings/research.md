# Research: Dashboard and Settings

## R1. Which refusals the page can predict

- **Decision**: predict from the status document's `roof` section and `wifi.apMode` only:
  - Open: `fault`, `state` open/opening, `weather` not `safe`, `wifi.apMode`, `supplyLow`,
    `hall.level` `unsettled`;
  - Close: `fault`, `state` closed/closing, `closePending`, `parkConfirmed` false, `supplyLow`,
    `hall.level` `unsettled`.
- **Rationale**: these are the device's own inputs to `RoofIo::checkCommand` and spec 002's
  `Controller::check` (`weatherNotSafe`, `mountNotParked`, `faultLatched`, `supplyLow`,
  `supplyUnreadable`, `hallNotSettled`) and spec 010's hotspot blocker. `weather` and
  `parkConfirmed` are what the checks see now (spec 008), stale hand-over included.
- **Not predicted**: `busy` (an upload, a save, an MQTT connect, a restart scheduled) and
  `restarting` aren't in the status; they are shown when the device answers. `currentUnreadable`
  and `settingsInvalid` are rare and shown from the answer. A person may move from an unknown
  position, so `positionKnown` false never disables.
- **Alternatives**: send and show the answer only (rejected: constitution VII wants the reason on a
  greyed button before the press); add a `blockers` object to the status (rejected: 008 owns the
  status producers, and the device refuses anyway).

## R2. Live data

- **Decision**: "live" means a status has arrived, the socket is open and the last message is at
  most 5 s old (`useQuiet`'s threshold, the one the Status card uses). Without it Open and Close are
  disabled; Stop is not.
- **Rationale**: the device pushes `/ws/status` every 2 s, and at once (at most every 250 ms) when the
  roof's state, fault or close phase changes; 5 s is already the dashboard's stale rule.

## R3. Refusal handling

- **Decision**: one command result at a time `{command, outcome, reason, text, at}`. It is shown
  until the roof's state changes or 15 s pass. `restarting` disables Open and Close until a message
  arrives after the socket was lost and reconnected, or 30 s pass. `busy` leaves the buttons usable.
- **Rationale**: a restart drops the connection; a fresh message after reconnect proves the device
  is back. `busy` lasts a few seconds (the device's own text says what).

## R4. Stop on every page

- **Decision**: a bar pinned to the bottom of `Layout`'s main area, rendered only while the last
  status says `opening`, `closing` or `closePending`. It reuses the save bar's surface. When both
  show, the save bar moves up by the Stop bar's height (`has-stop-bar` on the shell, as
  `has-demo-panel` already does).
- **Rationale**: DS-01 keeps the header and the space above the cards clear; the save bar already
  sets the pattern for a pinned action bar. Not shown at rest, so it never adds noise.
- **Alternatives**: a Stop button in the header (rejected: DS-01); only on the dashboard (rejected:
  constitution VII).

## R5. Hall scale

- **Decision**: an inline SVG bar 0 to 4095 with the reading as a marker and the low and high
  thresholds as ticks; `role="img"` with an `aria-label` stating the reading, its level and the
  thresholds. Colours from the existing tokens.
- **Rationale**: no chart library; a few elements; it doubles as the calibration aid in Settings.

## R6. Position bar

- **Decision**: the existing `progress-meter` look with `role="meter"` (not a progress bar: it
  isn't a task), `aria-valuetext` "45 % open" or "Unknown".

## R7. Saving the roof sections

- **Decision**: `toConfigPayload` copies `roof`, `safety` and `park` when present. The device
  accepts partial updates; a section it didn't send is never invented.
- **Note**: the device refuses any save while the roof is opening or closing (spec 010), and a save
  while stopped part-way makes the position unknown (spec 002). The tab says both.

## R8. Status card roof rows

- **Decision**: `roof-fault` (problem, the fault's text behind "?", link to the dashboard) and
  `roof-close` for `heldByPerson` (note), `cannotMove` and `halted` (problem). No roof tile.
- **Rationale**: DS-07/DS-08: a lasting problem that affects observing is a row on the Status card
  as well as a note in its own card; the state itself is the Roof card's.
