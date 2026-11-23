# Feature Specification: Park Source and SQMeter Secrets

**Feature Branch**: `spec/024-park-source-and-secrets`

**Created**: 2026-11-23

**Status**: Draft

## Context

Two gaps found while setting up a real observatory.

**The park source.** Today the mount counts as parked only when N.I.N.A. reports `AtPark` *and*
every fitted park switch reads closed (`park.switches`: none, one or two). The switches can only
ever add to N.I.N.A.; they can never replace it. An observatory whose mount has reliable reed
switches at its park position, or whose imaging PC is not always running N.I.N.A., has no way to
close the roof on the switches alone. This spec adds a park source with three choices:

- **N.I.N.A. only**: today's "no park switches".
- **Park switches only**: one or two switches confirm the park; with two, both must read closed.
  N.I.N.A. is not needed. If an address is set it is still asked to park when the weather turns
  unsafe, and a fresh reading that says the mount is slewing still counts against the park.
- **N.I.N.A. and park switches**: today's "one" or "two" switches.

A switch is a single wire. A broken wire reads "not parked", which is safe; a shorted wire or a
switch stuck closed reads "parked" for ever, which is not. With N.I.N.A. in the rule, a stuck
switch shows up as a disagreement the first time the mount unparks. With the switches alone it
does not, so this spec adds guards for that case (see FR-006 to FR-009) and states the risk in a
MAJOR constitution amendment (Principle II, 5.0.0).

**SQMeter's secrets.** Spec 020's "Copy from SQMeter" copies the MQTT broker, port, user name and
topic and the ntfy server and topic, but never a password, token or key, and never an on/off
switch. After a copy, MQTT looks untouched: still off, with no password. A matching SQMeter change
adds `GET /api/secrets`, which hands SQMeter's own MQTT password, ntfy token and Pushover keys to a
client that logs in with SQMeter's web login. This spec makes the copy and the sync use it, on the
device, so the secrets never reach the browser, and offers SQMeter's on/off switches as rows.

## Clarifications

### Session 2026-11-23

Each answer takes the safest option the constitution allows and is also listed under "Assumptions
decided for the owner".

- Q: How is the park source stored, and how do existing settings move over? → A: A new setting
  `park.source` (`nina`, `switches`, `ninaAndSwitches`) beside `park.switches` (how many are
  fitted). Settings without a source take it from the switches: none → `nina`, one or two →
  `ninaAndSwitches`. That is exactly today's rule, so no upgraded device confirms a park any
  differently. A save that sends `park.switches` without `park.source` (an older page or a
  script) gets the same rule, so it keeps its old meaning.
- Q: Which combinations are allowed? → A: `nina` with no switches; `switches` and
  `ninaAndSwitches` with one or two. A save with any other combination is refused, saying which to
  change. Stored settings that break the rule (edited by hand) still load and are evaluated safely:
  a fitted switch always counts, and "switches only" with none fitted never confirms a park.
- Q: Is the N.I.N.A. address needed with switches only? → A: No. With an address, the automatic
  close still asks N.I.N.A. to park (when `park.requestPark` is on) and then waits for the
  switches; without one it only waits for the switches, up to `park.timeoutSeconds`.
- Q: Does N.I.N.A. count at all with switches only? → A: Only against the park: a good, fresh
  reading (within 10 s) that says the mount is slewing means not parked, because a moving mount
  is not parked whatever a switch says. N.I.N.A. being unreachable, stale, disconnected, or not
  at its own park position doesn't count against it (the owner chose the switches as the truth).
- Q: The wiring fault asymmetry? → A: Four guards together, the safest workable set:
  1. **Wiring**: a switch closes to ground when the mount is parked (normally open, the existing
     pull-up input), so a broken wire or a loose connector reads "not parked". Normally-closed
     wiring is not offered: it would make the commoner fault, a broken wire, read "parked".
  2. **Two switches recommended**: the settings card, the wizard and the docs recommend two for
     switches only, wired on separate cables, and warn with one that a short or a stuck switch
     can't be told from a parked mount. One is allowed (the owner's choice).
  3. **Stuck-switch check**: with switches only, a fitted switch that hasn't once read open over
     3 roof openings in a row is suspect, and Open is refused until it has been seen open (the
     mount moved off its park, or the magnet taken away, once). The count survives a restart. The
     roof is closed when this bites, so the guard never leaves the roof open in the rain.
  4. **Slew veto**: with an address set, a fresh N.I.N.A. reading that says slewing is not parked.
- Q: Why refuse Open rather than the close? → A: Refusing a close leaves the roof open in bad
  weather. A switch that has been stuck for several nights is found while the roof is closed, so
  the owner is told before it next matters, at a moment when refusing costs nothing.
- Q: How do the test box bypass (022), the ride-through (021) and the jog, teach and calibration
  gates (019, 016) fit? → A: They all read the one park result. The test box's park bypass stands
  in for N.I.N.A. only, so it can't be set with switches only (the save is refused; stored, it has
  no effect). With switches only there is no N.I.N.A. reading to ride on: the interlock re-checks
  the switches every control step and nothing is bounded by N.I.N.A.'s freshness, except that a
  fresh slewing reading stops a close like any lost park.
- Q: Which on/off switches does the copy offer? → A: SQMeter's MQTT on/off, and its ntfy and
  Pushover alert channels' on/off. A row is offered only when SQMeter's is on and RoofRoof's is off;
  ticking it turns RoofRoof's on. Nothing in the copy or the sync ever turns one off. While a group
  follows SQMeter, the sync turns RoofRoof's switch on whenever SQMeter's is on.
- Q: How do the secrets get from SQMeter into the settings without reaching the browser? → A: The
  device reads `GET /api/secrets` during the copy, right after SQMeter's settings, and keeps what it
  read in memory for 30 minutes with the servers they belong to (SQMeter's broker, port and user
  name; its ntfy server). The copy's answer says only which secrets were received and whether each
  equals RoofRoof's saved one. A ticked row sets the secret field to a placeholder meaning "the one
  received from SQMeter"; the device's save swaps it for the held value only if the field's server
  is still the one it was received for, and refuses the save otherwise. The placeholder can never
  be stored.
- Q: When does the device fetch the secrets? → A: Only from the saved SQMeter address (its scheme,
  host and port), with the saved or typed SQMeter login. A copy from a typed, unsaved address gets
  no secrets and says to save the address first.
- Q: And the sync? → A: After each good read of SQMeter's settings, while the MQTT or the alerts
  group follows, the device reads the secrets too (in the next poller pass, so no pass grows). It
  fills the followed group's secrets with SQMeter's non-empty values, for the server they belong
  to only. It never clears one from the secrets (an empty value from SQMeter leaves RoofRoof's as
  it is); a server change still clears the old secret, as today. The alerts group now also covers
  Pushover's keys and switch.
- Q: What does the person see when the secrets can't be read? → A: Exactly why, and today's
  "enter the password" prompt: 404 → "Update SQMeter to share passwords"; 403 "Password sharing is
  off" → "SQMeter's password sharing is off: turn it on in SQMeter's Settings"; 403 "Set a
  web password" → "Set a web password in SQMeter first: it guards the passwords it shares"; 401 →
  "SQMeter refused the login: check SQMeter's user name and password here"; no reply or an
  unreadable one → the error. Over plain http a note says the passwords crossed the LAN
  unencrypted, as MQTT's own login does.

## Constitution rules this spec relies on (and the one it changes)

| Principle | Rule | Where it is kept |
|---|---|---|
| I | Motion faults, the watchdog, timeouts, supply | Unchanged: the park source only changes what the park result reads |
| II | Close only onto a confirmed park, every path, re-checked through the close | **Amended (5.0.0, MAJOR)**: the owner may make fitted park switches the only source, with the guards in FR-006 to FR-009; one park result for every path (FR-004) |
| II | Disagreement or an unreadable source is not parked | Kept: with two switches both must read closed; unreadable is not parked; a fresh slewing reading is not parked |
| III | Unsafe ends closed: park request, wait, close | Kept: the request goes out only with an address; the wait reads the park result (FR-005) |
| III | Alerts and the UI never change the verdict | Kept: the copy and sync touch only MQTT and alert settings |
| V | Safety rules in `lib/` with native tests | FR-016 |
| VII | Open says why it is unavailable | FR-008's refusal text |
| VIII | Secrets masked in responses, kept when sent back masked; actions need `requireAuth` | FR-013, FR-014: secrets never in a response; the copy and save are authenticated |
| IX | Docs move with behaviour | FR-017 |

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Close on the park switches alone (Priority: P1)

An observatory whose mount has two reed switches at its park position chooses "Park switches
only". The roof closes when both switches read closed, whether or not N.I.N.A. is running.

**Independent Test**: on the demo or natively, set the source to switches with two fitted and no
N.I.N.A. address; Close is accepted with both closed and refused with either open or unreadable.

**Acceptance Scenarios**:

1. **Given** switches only, two fitted, no N.I.N.A. address, **When** both read closed, **Then** the
   park is confirmed and Close (web, Alpaca, MQTT, the automatic close), jog, teach and a
   calibration leg pass the park check.
2. **Given** the same, **When** either switch reads open or unreadable, **Then** the park is not
   confirmed and a close under way stops as a park-interlock fault.
3. **Given** switches only and an address, **When** N.I.N.A. is unreachable or says not at park,
   **Then** the switches alone decide; **When** a fresh reading says slewing, **Then** not parked.
4. **Given** switches only and unsafe weather, **When** an address is set and park requests are on,
   **Then** N.I.N.A. is asked to park and the close waits for the switches; **When** no address is
   set, **Then** it only waits for the switches, and after `park.timeoutSeconds` raises the critical
   "mount not parked" alert naming the switch.

---

### User Story 2 - Upgrade changes nothing (Priority: P1)

**Acceptance Scenarios**:

1. **Given** settings saved with `park.switches` none, one or two, **When** the new firmware loads
   them, **Then** the source is N.I.N.A. only, N.I.N.A. and switches, N.I.N.A. and switches, and
   every park decision is the same as before.
2. **Given** an older page or script, **When** it saves `park.switches` alone, **Then** it keeps the
   meaning it had before.

---

### User Story 3 - A stuck or shorted switch is caught (Priority: P1)

**Acceptance Scenarios**:

1. **Given** switches only, **When** the roof has been opened 3 times in a row without a fitted
   switch ever reading open, **Then** Open is refused, saying which switch and how to clear it.
2. **Given** that refusal, **When** the switch reads open once (the mount moved off its park),
   **Then** Open is allowed again; the count also restarts after a restart only from its saved value.
3. **Given** switches only with one switch, **When** the owner views the park settings or the
   wizard, **Then** a warning recommends two switches on separate cables.
4. **Given** N.I.N.A. only or N.I.N.A. and switches, **Then** the stuck-switch check never refuses.

---

### User Story 4 - The wizard asks how the park is known (Priority: P2)

**Acceptance Scenarios**:

1. **Given** the wizard's Hardware page, **When** the park screen shows, **Then** it asks "How does
   RoofRoof know the mount is parked?" with three choice cards, then how many switches (one or
   two) unless N.I.N.A. only was chosen.
2. **Given** switches only, **Then** the N.I.N.A. screen says its address is optional.

---

### User Story 5 - Copy brings MQTT across fully (Priority: P1)

The owner copies from SQMeter; MQTT ends up on, with SQMeter's broker and password.

**Acceptance Scenarios**:

1. **Given** SQMeter shares its passwords, **When** the copy runs, **Then** the MQTT broker row says
   the password was received, an "MQTT on" row is offered while RoofRoof's MQTT is off, and after
   ticking both and saving, RoofRoof's MQTT is on with SQMeter's password; the browser never sees
   the password.
2. **Given** the broker is already the same but RoofRoof has no password (an earlier copy),
   **Then** an "MQTT password" row is offered.
3. **Given** SQMeter's ntfy token and Pushover keys, **Then** they come across the same way with
   their rows, and the ntfy and Pushover "on" rows are offered while RoofRoof's are off.
4. **Given** the person ticks the broker row, then edits the broker before saving, **Then** the save
   is refused: the password is only for the broker it came with.
5. **Given** SQMeter can't share (404, 403, 401), **Then** the copy says exactly why and asks for
   the password as today.
6. **Given** an http address, **Then** a note says the passwords crossed the LAN unencrypted.

---

### User Story 6 - Sync keeps them current (Priority: P2)

**Acceptance Scenarios**:

1. **Given** the MQTT group follows, **When** SQMeter's MQTT password changes, **Then** RoofRoof's
   follows at the next sync (within about 10 minutes).
2. **Given** the alerts group follows and SQMeter turns ntfy on, **Then** RoofRoof's ntfy turns on.
3. **Given** a following group and SQMeter not sharing, **Then** the status says why and the
   settings ask for the secret as today.

### Edge Cases

- Switches only with none fitted (stored by hand): never parked, "no park switch fitted".
- N.I.N.A. only with a switch fitted (stored by hand): the switch still counts.
- Changing the source while the roof is closing: a motion setting change, refused while busy.
- The stuck count reaches 3 while a switch reads unreadable: unreadable doesn't clear it.
- The held secrets expire (30 min) before the save: the save is refused, saying to copy again.
- SQMeter returns an empty password: "not set"; no row, nothing cleared.
- The copy's address is typed but not saved: no secrets; "Save SQMeter's address first".
- The sync reads secrets while MQTT roof commands are on: the MQTT group is held, as today.
- A restore of a backup taken before this spec: the park source is derived from its switches.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Settings gain `park.source`: `nina`, `switches` or `ninaAndSwitches`. Settings and
  saves without it take it from `park.switches` (none → `nina`, one or two → `ninaAndSwitches`).
- **FR-002**: A save is refused when `nina` comes with a fitted switch, or `switches` or
  `ninaAndSwitches` with none, or when the test box's park bypass is set with `switches`.
- **FR-003**: With `switches`, the park is confirmed when every fitted switch reads closed and no
  good reading within N.I.N.A.'s freshness limit says the mount is slewing. N.I.N.A.'s other states
  don't count. With none fitted it is never confirmed. `nina` and `ninaAndSwitches` are today's rule.
- **FR-004**: The one park result feeds every close path, the automatic close, the interlock, jog,
  teach and the calibration run. With `switches` there is no ride-through and no freshness bound
  from N.I.N.A.; a lost park stops a close at the next control step as today.
- **FR-005**: The automatic close asks N.I.N.A. to park only when an address is set (and park
  requests are on); otherwise it waits for the park. The status and alert texts name the switch.
- **FR-006**: The park switches are wired to close to ground at park (the existing pull-up input);
  the docs say why normally-closed wiring is not supported.
- **FR-007**: With `switches` and one switch, Settings and the wizard warn and recommend two.
- **FR-008**: With `switches`, each fitted switch keeps a count of roof openings (the roof reaching
  open, not during a calibration run) since it last read open. At 3 or more, Open is refused with
  "Park switch N hasn't opened in the last 3 roof openings: it may be stuck or shorted. Move the
  mount off its park (or take the magnet away) once to check it". Reading open sets the count to 0.
  The counts are kept across restarts (written only while the roof is still).
- **FR-009**: The status reports each switch's count and whether the check refuses Open.
- **FR-010**: The copy's site document gains SQMeter's MQTT, ntfy and Pushover on/off states, and
  a `secrets` part: which of the MQTT password, ntfy token and Pushover keys were received, whether
  each equals RoofRoof's saved one, the problem when they weren't, and whether the address is http.
- **FR-011**: The copy offers: "MQTT on", "ntfy on", "Pushover on" (SQMeter's on, RoofRoof's off);
  "MQTT password" and "ntfy token" (same server, received, differs); "Pushover keys" (received,
  differ). The broker and ntfy rows carry the received secret instead of clearing it.
- **FR-012**: The device fetches `GET /api/secrets` only from the saved SQMeter address's scheme,
  host and port, with the copy's login, and holds the result in memory for 30 minutes with the
  servers it belongs to.
- **FR-013**: A secret field sent as the placeholder `(from SQMeter)` is replaced at the save by the
  held value, only when the field's server equals the one it was received for and the hold hasn't
  expired; otherwise the save is refused. The placeholder is never stored or returned.
- **FR-014**: No response carries a secret's value; the copy says "password received".
- **FR-015**: While the MQTT or alerts group follows, the device reads the secrets after each good
  settings read and fills the group's secrets with SQMeter's non-empty values for the matching
  server; it turns on a switch SQMeter has on; it never clears a secret from the secrets read or
  turns a switch off. The sync status reports why secrets couldn't be read.
- **FR-016**: The park rule, the source rules, the stuck-switch count, the secrets parse, the fill
  and the sync are pure `lib/` code with native tests; the demo uses the same code and its
  simulated SQMeter serves `/api/secrets`.
- **FR-017**: Docs: safety inputs (park), wiring, commissioning, configuration, REST, wizard, the
  SQMeter site page; the constitution amendment; OpenAPI and the contract schemas.

### Key Entities

- **Park source** (`park.source`), **park switches fitted** (`park.switches`).
- **Switch watch**: per fitted switch, roof openings since it last read open (device-owned).
- **Held secrets**: SQMeter's secrets from the last copy, with their servers and time (memory only).
- **Secrets read**: the sync's last read of SQMeter's secrets, or why it failed (memory only).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Every existing park test passes unchanged with settings migrated from `switches`.
- **SC-002**: With switches only, Close is accepted with all fitted switches closed and refused
  with any open or unreadable, with or without N.I.N.A. (native tests at each combination).
- **SC-003**: A stuck switch refuses Open at the 4th opening and never before (native boundary test).
- **SC-004**: After one copy with sharing on, MQTT is on with SQMeter's password in one save, and no
  response body contains a secret (native, Vitest and Playwright on the demo).
- **SC-005**: Each of 404, both 403s and 401 shows its own message (native and Vitest).

## Assumptions decided for the owner

Each was an open choice; the safest option the constitution allows was taken.

1. **Three sources in one setting** (`park.source`) beside the switch count, upgraded exactly from
   `park.switches`; a save with only the count keeps its old meaning, and Settings always sends both.
2. **One switch is allowed with switches only**, with a warning recommending two on separate
   cables. Requiring two would be safer but would turn away the owner's stated one-switch case;
   the stuck-switch check covers the long-lived single-switch fault.
3. **Normally-open wiring only** (closes to ground at park). Normally-closed would make a broken
   wire, the commoner fault, read "parked".
4. **Stuck-switch check: 3 roof openings**, refusing Open (never the close), cleared by the switch
   reading open once, counted when the roof reaches open (not during a calibration run), persisted.
   A short that appears during a night, with one switch, can't be detected; that is the stated
   risk of the amendment, and why two switches are recommended.
5. **Slew veto only**: with switches only, a fresh N.I.N.A. reading of "slewing" means not parked;
   "not at park", stale, unreachable or disconnected don't count. The owner chose the switches so
   that N.I.N.A.'s state doesn't hold the roof open.
6. **The test box park bypass can't be set with switches only**: it stands in for N.I.N.A., which
   that source doesn't use.
7. **Switches only turn on.** The copy and the sync only ever turn RoofRoof's MQTT, ntfy or Pushover
   on; neither turns one off (turning off an alert channel or the MQTT weather source silently
   would weaken the alert path). While a group follows, a switch SQMeter has on is turned back on
   at the next sync; to keep it off, stop following the group.
8. **Secrets held 30 minutes in memory**, never written until the person saves; a restart drops them.
9. **The sync never clears a secret from the secrets read**: an empty value from SQMeter leaves
   RoofRoof's as it is (a Pushover key set only here survives).
10. **Secrets only from the saved address**: a typed, unsaved address gets no secrets.
11. **Constitution 5.0.0 (MAJOR)** on top of 4.0.0 (spec 023).
12. **No new alert event** for the stuck-switch check: it refuses Open with its reason on every
    surface that shows Open's reason; an alert can follow in a later spec if wanted.
13. **The UI was not mocked up for owner review before building**; it reuses the choice card group,
    Note, Toggle and the copy's existing rows; the owner reviews it on the PR.
14. **The secrets read pairs with the settings read** taken one poller pass before it; a broker
   change on SQMeter between the two (about 0.1 s) is the only window where a password could be
   matched to the previous broker of that same SQMeter.
15. **No automatic retry after a 401**: the sync stops asking until the saved login changes; a 429
    waits for the next settings read. The copy (a person's action) asks once each time.
