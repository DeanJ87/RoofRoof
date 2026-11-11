# Feature Specification: Wi-Fi and OTA Under Motion

**Feature Branch**: `spec/010-network-under-motion`

**Created**: 2026-11-11

**Status**: Draft

## Context

The roof moves for about 50 s at a time, and the automatic close (spec 005) can run for many
minutes while it waits for the mount to park. Meanwhile the rest of the device keeps working: the
web server takes settings saves and uploads, the setup hotspot can open, MQTT reconnects, and the
device can be asked to restart. This spec makes sure none of that can move the roof, prolong a
move, interrupt a close or leave the roof driving without control.

Much of it already exists. Spec 003 added the hold-off rules while the roof moves (its FR-024 to
FR-031), and spec 005 made the close sequence survive a restart. This spec audits those against the
roadmap item and closes the gaps it found.

### Already covered before this spec

| Roadmap item | Covered by | Where |
|---|---|---|
| Settings saves, restores, uploads, Wi-Fi changes and restarts refused while the roof moves | 003 FR-024 | `WebServer::refuseWhileMoving`, `RoofIo::platformActionRefused` |
| A scheduled restart waits while the roof moves or a close waits | 003 FR-024, FR-029 | `restartTimerCallback`, `WebServer::holdRestarts` |
| A close always wins over an upload, an MQTT connect and a scheduled restart | 003 FR-029 | `RoofIo::blockerDecision`, `RoofControl::setBlockers` |
| Command-line OTA only while the roof is closed | 003 FR-030 | `RoofIo::commandLineOtaAllowed` |
| No MQTT connect while the roof moves or a close waits; MQTT reads give up after 3 s | 003 FR-029 | `MQTTClient::setConnectHold` |
| The control task runs on its own, above the web server, with a hard timeout per move and a watchdog that drops EN | 002, 003 FR-005 to FR-009, FR-025, FR-028 | `RoofControl`, `MotorDriver` |
| Weather and park hand-over older than 5 s: an open stops, a close stops with a fault | 003 | `RoofIo::currentGates`, `test_roof_gates` |
| A boot between the magnets reads unknown and never moves by itself | 002, 003 | `test_boot_between_magnets_is_unknown_and_off` |
| A fault, and a watchdog trip, survive a restart | 003 | `RoofFaultStore`, the RTC trip marker |
| The close sequence's stand-down and attempt time survive a reset | 005 | `CloseOnUnsafe` RTC record |
| A reconnect to Wi-Fi writes no flash | 003 | `WiFi.persistent(false)` |

### Gaps this spec closes

1. **Check-then-act races.** Every platform action checks "is the roof moving?" and then acts, and
   every roof command checks "is anything busy?" and then moves, each without the other's lock. A
   save, an upload, a restart or an MQTT connect that starts in the same instant as a move can
   still overlap it (spec 003 recorded this as a known limit).
2. **Short flash writes are invisible to a close.** A settings save, a restore, the Wi-Fi
   credentials saved after a join, the alerts pause state and the roof's own fault record all write
   the flash, but only uploads count as "the flash is busy". A close accepted during one of those
   writes starts at once, and its first steps run with the code cache stalled.
3. **Restarts and firmware updates during the automatic close.** Restarts were only held while the
   roof moves. A restart while the sequence waits for the park costs the wait (spec 005 counts it),
   and a firmware update may not read spec 005's RTC record, so a person's stand-down could be
   lost and the sequence start over.
4. **A Wi-Fi change during the automatic close** drops the network that N.I.N.A. and SQMeter are
   reached over, so the park can't be confirmed and a close in progress stops with a fault.
5. **The setup hotspot has no password.** When it opens (the saved network not joined within
   45 s of start-up, typically after a power cut that also took the router down), anyone in radio
   range can reach the web UI, and with password protection off could open the roof.
6. **MQTT settings saves reconnect from the web server's task** (spec 004 FR-011a known limit): the
   reconnect races the main loop's use of the MQTT client, blocks the web server for up to 3 s plus
   DNS, and lets a retained message count as live.
7. **Command-line OTA's end**: the flash write is marked finished just before the device restarts,
   leaving a moment in which an open could start and then be cut off by the restart.

## Clarifications

### Session 2026-11-11

Each answer takes the safest option the constitution allows and is also listed under "Assumptions
decided for the owner".

- Q: Should a restart, an upload or a Wi-Fi change be allowed while the automatic close is running
  but the roof isn't moving (waiting for the park, or backing off after a failed attempt)? → A:
  Firmware and web UI uploads, restarts other than the one after a web UI upload, Wi-Fi changes
  and scans are refused (409) while the close sequence is working: waiting for the park it asked
  for, or closing; a restart already scheduled waits. A firmware upload is also refused while a
  person has stood the sequence down. Backing off after a failed attempt, or stood down for an
  unknown position, a fault or a halted close, the sequence holds nothing: it can stay there for
  hours, often because the network is broken, and a Wi-Fi fix, a restart or an update must stay
  possible. Settings saves and restores without a restart still work.
- Q: Should spec 005's stand-down and attempt state move from RTC memory to NVS? → A: No. Keeping
  the running time in NVS would mean writing the flash every few seconds, which the roof forbids
  while it moves. RTC memory survives every reset of the same firmware; the one case it may not
  survive, a new firmware, is now refused while the sequence works or a person has stood it down. A power cut loses it,
  as spec 005 accepted.
- Q: What may be done while the setup hotspot is on? → A: Everything except opening the roof, from
  any network (a client on the hotspot can reach the device's other addresses too). Stop is never
  refused (constitution I), and closing and clearing a fault help the roof end closed. Opening
  always needs a safe verdict, which needs the home network anyway.
- Q: When Wi-Fi drops mid-move, should the move stop? → A: No. The move goes on under the control
  task's own timeout and magnets; what Wi-Fi loss takes away (a stale weather or park hand-over, a
  lost N.I.N.A. poll) already stops an open and stops a close with a fault, as constitution II
  requires. Nothing new is added on Wi-Fi loss itself.
- Q: What does a restart that has already been committed do to a close? → A: For the moment
  between deciding to restart and the reset, an open or close is refused ("The device is
  restarting"); stop is still accepted. Ordinary restarts never get that far while the close
  sequence is active, so in practice only the restart after a web UI upload meets this.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Nothing on the network can overlap a move (Priority: P1)

The owner saves settings, uploads firmware or restarts the device from the web UI at the same
moment as the automatic close or a client's command starts the roof. Either the platform action is
refused or held, or the roof command waits; they never overlap.

**Why this priority**: an overlap stalls the control task (flash) or cuts power to the driver
mid-move (restart). Constitution I.

**Independent Test**: native tests of the hold rules and the flash-activity count; on the board,
saving settings and closing in a loop for a minute never logs a step gap above the limit.

**Acceptance Scenarios**:

1. **Given** the roof is at rest, **When** a settings save and a close arrive together, **Then**
   either the save runs and the close waits for it (accepted, `pending`) and starts within one step
   of the save ending, or the close runs and the save is refused (409, "The roof is moving").
2. **Given** a settings save is writing, **When** an open arrives, **Then** it is refused (`busy`,
   "A settings save is being written: try again in a moment").
3. **Given** the roof is closed and the device decides to restart, **When** a close or open
   arrives after the decision, **Then** it is refused ("The device is restarting"); stop is still
   accepted.
4. **Given** a close command is in progress, **When** the restart timer fires, **Then** the restart
   waits and tries again; it never runs while the roof moves or a close waits.
5. **Given** the roof's fault record needs saving, **When** a command arrives during the save,
   **Then** an open is refused and a close waits, as for any other flash write.

---

### User Story 2 - The automatic close is never interrupted by the platform (Priority: P1)

While the weather is unsafe and the roof is open, the sequence asks N.I.N.A. to park, waits and
closes, possibly for many minutes. A restart, a firmware upload or a Wi-Fi change in that time
would cost the wait, could lose a person's stand-down, or would drop the network the park check
needs.

**Why this priority**: constitution III, unsafe ends closed.

**Independent Test**: native tests of the hold rules in each sequence state; on the board, with the
sequence waiting for the park, `POST /api/restart`, a firmware upload and `POST /api/wifi/connect`
each answer 409 naming the close sequence.

**Acceptance Scenarios**:

1. **Given** the close sequence is waiting for the park, **When** a person asks for a restart,
   **Then** it is refused (409, "The automatic close is under way: …").
2. **Given** the close sequence is working or a person has stood it down, **When** a firmware upload starts,
   **Then** it is refused before anything is written; command-line OTA likewise.
3. **Given** a firmware upload finished just before the sequence started working, **When** its
   restart comes due, **Then** it waits until the sequence stops working (or a close cancels it).
4. **Given** the sequence is working, **When** a web UI upload starts, **Then** it is refused (its
   restart could not be cancelled).
5. **Given** the sequence is working, **When** someone tries a new Wi-Fi network or a scan,
   **Then** it is refused (409) and the current connection is kept.
6. **Given** the sequence is backing off or stood down for an unknown position, a fault or a halted
   close, **When** a restart, a Wi-Fi change or an update arrives, **Then** it is not refused for
   the sequence (the way out stays open).
7. **Given** the sequence is working, **When** a settings save or a restore without restart arrives,
   **Then** it is accepted (subject to story 1).

---

### User Story 3 - The open setup hotspot can't open the roof (Priority: P2)

After a power cut, the router may come back later than the device, and the device opens its setup
hotspot (no password). Anyone in range can join it.

**Why this priority**: constitution VIII (trusted LAN) and III (opening is a deliberate act).

**Independent Test**: native test of the rule; on the board, with the hotspot open, `POST
/api/roof/open` from a phone on the hotspot answers 409 naming the hotspot; close, stop and
clear-fault answer as from the home network.

**Acceptance Scenarios**:

1. **Given** the setup hotspot is on, **When** an open arrives over any network, **Then** it is
   refused (409, `reason` `hotspot`, "Opening isn't available while the setup hotspot is on").
2. **Given** a request arrives over the setup hotspot, **When** it is a stop, close or clear-fault,
   **Then** it is handled exactly as from the home network (authentication, header and gates).
3. **Given** the hotspot is off, **When** an open arrives, **Then** it is not refused for the
   hotspot.

---

### User Story 4 - MQTT settings take effect on the main loop (Priority: P2)

Saving new MQTT settings reconnects the broker. Today that runs on the web server's task.

**Why this priority**: the MQTT client is not thread-safe, the web server must not block for
seconds, and spec 004's retained-message rule depends on the reconnect being seen on the loop.

**Independent Test**: on the board, saving the MQTT settings returns at once and the log shows the
reconnect from the main loop on its next pass; native: none (glue only, see plan).

**Acceptance Scenarios**:

1. **Given** MQTT settings are saved, **When** the save returns, **Then** no MQTT connect has run
   on the web server's task; the main loop applies the settings on its next pass and reconnects,
   subject to the roof's connect hold.
2. **Given** two saves arrive before the loop runs, **When** the loop runs, **Then** only the newer
   settings are applied.
3. **Given** the roof is moving, **When** MQTT settings are saved, **Then** the save is refused as
   any other (story 1); a save at rest whose reconnect comes due while the roof moves waits for the
   roof as any reconnect does.

---

### User Story 5 - Losing Wi-Fi or power mid-move ends safely (Priority: P3)

**Why this priority**: already covered by specs 002, 003 and 005; this story records the behaviour
and its tests and adds the reset reason to the log so a brown-out mid-move can be diagnosed.

**Independent Test**: the existing native tests listed under "Already covered"; bench checks.

**Acceptance Scenarios**:

1. **Given** the roof is closing, **When** Wi-Fi drops, **Then** the move continues under its own
   timeout and magnets; when the park result can no longer be confirmed it stops with a
   `parkInterlock` fault (constitution II); it never drives past its timeout.
2. **Given** the roof is opening, **When** Wi-Fi drops, **Then** the open continues until the
   weather hand-over goes stale or a source says unsafe, then stops.
3. **Given** the device resets mid-move (brown-out, crash, watchdog), **When** it starts again,
   **Then** the driver is off, the position is unknown unless a magnet is read, nothing moves by
   itself, and the log names the reset reason (for example "brown-out").

---

### Edge Cases

- A close that waits for a settings save: the save takes tens of milliseconds; the close starts on
  the first control step after it ends (no 5 s staleness applies to short writes, which always
  end).
- A settings save while an upload writes: both count as flash activity; the upload is not stopped
  by the save and the save is not refused by the upload.
- The restart timer fires while the roof's lock is held by a step: it doesn't wait for the lock; it
  tries again 100 ms later.
- A restart was committed but the device didn't reset (impossible in practice): open and close
  stay refused until the device resets; stop always works.
- The close sequence starts working during a firmware upload: the upload completes; its restart waits
  until the sequence is idle or a close cancels it.
- The hotspot and station are both up and a request's local address is the station's: it counts as
  the home network.
- An MQTT save while a previous save's reconnect is still pending: the newest settings win.
- The device resets with the roof between the magnets during a power cut that also takes the
  router: the hotspot may open after 45 s; the roof stays unknown and still; a person can close
  over the hotspot (reduced speed, stopping at the first magnet, as for any move from unknown).

## Requirements *(mandatory)*

### Functional Requirements

**One decision point for motion and platform actions**

- **FR-001**: A roof command's check of what else is busy (uploads, flash writes, an MQTT connect,
  a scheduled or committed restart) MUST be made under the same lock as the command itself.
- **FR-002**: A platform action that must not overlap a move (a flash write, a restart, an MQTT
  connect) MUST check that the roof isn't moving and no close waits under that same lock, and MUST
  register itself (flash activity, committed restart, connect in flight) before the lock is
  released, so a command decided after it sees it.

**Flash writes**

- **FR-003**: Every flash write MUST count as flash activity while it runs: settings saves,
  restores, the Wi-Fi credentials saved after a join, the alerts pause state, the roof's fault
  record, web uploads and command-line OTA.
- **FR-004**: Short writes (everything except uploads and command-line OTA) MUST NOT replace or stop
  an upload's activity, and an upload MUST NOT be stopped or refused by a short write.
- **FR-004a**: A Wi-Fi change and a Wi-Fi scan MUST register under the roof's lock as they start;
  until the change has joined the new network, or the scan has ended, open MUST be refused and a
  close MUST wait, as for a flash write, but never longer than 15 s from the start (well inside the
  close sequence's 30 s limit for a waiting close). Only one Wi-Fi change runs at a time, and it
  counts as joined only on the network being tried.
- **FR-005**: A flash write MUST be refused (409 for a request, deferred for a background write)
  while the roof moves or a close waits. Open MUST be refused (`busy`) and a close MUST wait
  (`pending`) while any flash write runs, as spec 003 FR-029 does for uploads.

**Restarts**

- **FR-006**: An ordinary restart (a person's, after a restore, after a firmware upload, after
  joining a network from the hotspot) MUST wait while the roof moves, a close waits, the close
  sequence is working, or a flash write runs. Every restart, the mandatory one included, MUST wait
  while a short flash write runs or a latched fault isn't saved yet (a restart never clears the
  lockout). A person's restart request MUST be refused (409) in those cases with the
  reason; a restore that needs a restart likewise.
- **FR-007**: The restart after a web UI upload (mandatory, spec 003) MUST wait while the roof moves
  or a close waits, and MUST NOT wait for the close sequence.
- **FR-008**: Once a restart is committed (the last check passed), open and close MUST be refused
  until the reset (reason `restarting`, "The device is restarting"); stop MUST be accepted. The
  automatic close MUST treat that refusal as "not run" (back to waiting for the park), not as a
  failed close.
  The restart timer MUST NOT block waiting for the roof's lock.
- **FR-009**: Command-line OTA MUST keep counting as flash activity until the device resets after
  a successful upload.

**Firmware uploads and Wi-Fi changes during the automatic close**

- **FR-010**: A firmware upload and a web UI upload (web and command-line; the web UI upload always
  ends in a restart nothing may cancel) MUST be refused before anything is written
  while the roof moves, a close waits or the close sequence is working, and a firmware upload also
  while a person has stood the sequence down (409 with the reason).
- **FR-011**: A change of Wi-Fi network (`POST /api/wifi/connect`) and a Wi-Fi scan MUST be refused
  while the roof moves, a close waits or the close sequence is working. Backing off or stood down,
  the sequence holds none of these, so the network or the device can be put right remotely; nor
  does a working sequence while the home network is down or the setup hotspot is on (it can't
  reach N.I.N.A. then). The refusal names the way out: Stop stands the automatic close down.
- **FR-011a**: A restart between the close sequence's Close action and the roof's answer MUST
  carry on from the step before (no failed close, no back-off).
- **FR-012a**: The hotspot refusal MUST be decided with the other command blockers under the roof's
  lock, so every route that opens the roof meets it.

**Setup hotspot**

- **FR-012**: An open command MUST be refused while the setup hotspot is on, whatever network it
  arrives over (409, reason `hotspot`). Stop, close and clear-fault over the
  hotspot MUST behave as from the home network. The rule MUST be a pure function so the Alpaca Dome
  (spec 006) applies the same rule.

**MQTT settings**

- **FR-013**: `MQTTClient::updateConfig` MUST only hand the new settings over; the main loop MUST
  apply them (disconnect, new server, connect subject to the roof's connect hold) on its next pass.
  The newest settings win. Reading the MQTT status from another task MUST NOT race the apply.
- **FR-014**: The spec 004 FR-011a known limit MUST be removed from the docs once FR-013 holds.

**Resets**

- **FR-015**: At start-up the log MUST name the reset reason (power-on, brown-out, software,
  panic, task or interrupt watchdog, other).

**Docs**

- **FR-016**: The roof-motion page MUST describe the platform hold rules in one table (action ×
  roof moving / close waiting / close sequence working); the OTA, first-setup, MQTT and REST pages MUST
  describe the new refusals; the spec 003 and 004 known limits that this spec removes MUST be
  marked as resolved where the docs repeat them.

### Key Entities

- **Platform action**: a flash write, an ordinary restart, the mandatory restart, a firmware
  upload, a Wi-Fi change.
- **Hold reason**: none, the roof is moving, a close waits, the automatic close is under way, the
  device is restarting.
- **Flash activity**: one upload or command-line OTA (with its token and staleness) plus a count of
  short writes in progress.

## Constitution rules relied on

| Rule | Requirement | Test |
|---|---|---|
| I: OTA, restarts, a restore and motion settings refused while moving | FR-005, FR-006, FR-010 | `test_platform_hold`: `test_every_action_held_while_moving`, `test_close_waiting_holds_every_action` |
| I: a busy network can delay a command, never prolong a move | FR-001, FR-002, FR-005 | `test_platform_hold`: `test_short_write_counts_as_busy`, `test_open_refused_close_deferred_during_short_write` |
| I: the control task independent of Wi-Fi and the web server | existing (003) | `test_roof_gates` (stale hand-over) |
| I: never move by itself while the position is unknown (after a reset) | existing (002) | `test_roof_logic`: `test_boot_between_magnets_is_unknown_and_off` |
| II: park re-checked throughout the close; unreadable counts as not parked | existing (003) | `test_roof_gates` |
| III: unsafe ends closed; nothing may interrupt the sequence needlessly | FR-006, FR-010, FR-011 | `test_platform_hold`: `test_sequence_busy_holds_restart_firmware_and_wifi`, `test_mandatory_restart_ignores_sequence` |
| III: alerts, MQTT and the UI never change the verdict | FR-013 | review (glue) |
| VIII: every state-changing endpoint authenticated; nothing implies internet exposure | FR-012 | `test_platform_hold`: `test_hotspot_refuses_only_open` |

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Every hold rule (each action against each reason, including the mandatory restart and
  the committed restart) and the short-write count have native tests, each of which fails if its
  rule is removed (checked by deliberate breakage, results in the PR).
- **SC-002**: On the bench, 200 alternating settings saves and close/stop commands sent together
  produce no step gap over the control limit and no watchdog trip (bench check).
- **SC-003**: On the bench, with the close sequence waiting for the park, restart, firmware upload
  and Wi-Fi change each answer 409 within 1 s and the sequence carries on (bench check).
- **SC-004**: On the bench, saving MQTT settings answers within 1 s even with the broker
  unreachable (bench check).
- **SC-005**: The full local check (firmware build with zero warnings, native tests, web tests,
  Playwright, quality gate, docs build, flash budget) passes; flash growth stays under 9 KB.

## Assumptions decided for the owner

Each was an open choice; the safest option the constitution allows was taken.

1. **Firmware and web UI uploads, ordinary restarts, Wi-Fi changes and scans are refused while the
   automatic close is working** (waiting for the park it asked for, or closing), and a firmware
   upload also while a person has stood it down. Backing off or stood down otherwise, the
   sequence holds nothing: it may stay there for hours because the network is broken, and the
   remote way out must stay open. A new firmware during such a stand-down may restart the
   sequence from scratch (its RTC record may not be read); that only repeats an attempt to park
   and close, which is the direction constitution III asks for.
2. **Spec 005's RTC record stays in RTC memory** (not NVS). Writing the attempt's running time to
   flash would mean frequent writes, some during moves; refusing a new firmware while the sequence
   works or a person has stood it down covers the cases that matter.
3. **Opening is refused while the setup hotspot is on; closing, stopping and clearing a fault are
   not.**
   The hotspot has no password. An open needs a safe weather verdict, which needs the home network,
   so nothing useful is lost; closing helps the roof end closed.
4. **A committed restart refuses close as well as open.** It lasts milliseconds, ordinary restarts
   are held while the sequence is working, and a refused close is retried by the sequence (or the
   person) after the restart. Letting the close start would only have it cut off by the reset.
5. **Wi-Fi loss mid-move adds no new behaviour.** The control task carries the move to its magnet
   or timeout; a stale hand-over or a failed N.I.N.A. poll stops it as before.
6. **The restart timer doesn't wait for the roof's lock**: it tries again after 100 ms, so it never
   delays the roof's watchdog timer, which shares the timer task.
7. **The reset reason is logged, not shown in the status document**: the status document belongs
   to spec 008.
8. **Known limits, accepted**:
   - the settings object itself is still written on the web server's task and read by the main
     loop (platform code from SQMeter). Saves are refused while the roof moves and now count as
     flash activity, so a save can't overlap a move; moving every apply onto the loop is a platform
     change for later (upstream candidate);
   - the close sequence's state is read without the roof's lock; a sequence that starts working
     during a firmware upload is covered by holding the restart (FR-006);
   - a firmware upload finished just before the sequence started working, whose restart a close then
     cancels, activates at the next restart; if that restart happens while the sequence is working
     (a crash), the RTC record may not be read and the sequence starts over;
   - the setup hotspot itself still opens while the roof is open: it is how the device is reached
     when the home network is gone; while it is on, the roof can't be opened from any network;
   - a Wi-Fi change or scan holds a close for at most 15 s; a close after that, with the network
     still not back, meets the park gate as usual (refused, or stopped with `parkInterlock` if
     N.I.N.A. drops part-way);
   - the close sequence's state is checked again under the roof's lock but is not changed under
     it, so an action can still begin in the instant the sequence leaves idle; a close refused for
     a committed restart is answered as "not run";
   - a fault record that can't be saved holds restarts until it is (the device keeps retrying);
   - a web UI upload erases its whole partition in its first chunk (SQMeter heritage); if that
     takes longer than 5 s the upload stops counting as busy and a close could start during the
     erase; sector-by-sector erasing is a later fix;
   - a fault latched in the few milliseconds between a restart's commit and the reset isn't
     saved (no write is started under a committed restart); an RTC copy of the fault is a later
     fix, as for any crash in that window;
   - the MQTT client is driven only on the main loop; other tasks read its state from the last
     loop pass. The alert dispatcher's MQTT channel is platform code from SQMeter and unchanged.

## Out of scope

- Alert events for a refused action or a reset mid-move (007), the Alpaca Dome's use of the
  hotspot rule (006), status document fields (008), a UI note for the refusals (009), bench mode
  (011).
