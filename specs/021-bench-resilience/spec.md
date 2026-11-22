# Feature Specification: Bench Resilience

**Feature Branch**: `spec/021-bench-resilience`

**Created**: 2026-11-22

**Status**: Draft

## Context

The first real board on the bench showed two defects that the simulator and the native tests did
not.

1. **One missed N.I.N.A. reply stops a close as a latched fault.** While the roof closes, the
   control task re-checks the park every step (constitution II). The park result counts any failed
   N.I.N.A. read (no reply, an unreadable reply, an API error) as "not parked", so a single HTTP
   timeout during a close latched a park-interlock fault and left the roof part-way open, perhaps in
   the rain. On the bench, "N.I.N.A. not responding: read Timeout" showed for about 3 s and then
   cleared. Nothing said the mount had moved: N.I.N.A. simply didn't answer one poll.
2. **A settings page that was loaded earlier overwrites newer settings.** A Settings tab opened
   before the Wi-Fi network and the N.I.N.A. address were changed from another client saved an
   unrelated change (IPv6 off). The page sends the whole settings document, so the save put back the
   old Wi-Fi network and cleared the N.I.N.A. address.

This spec fixes both, without weakening the park rule and without a new way to lose settings or
the Wi-Fi connection.

## Clarifications

### Session 2026-11-22

Each answer takes the safest option the constitution allows and is also listed under
"Assumptions decided for the owner".

- Q: Which N.I.N.A. results count as a read failure that a running close may ride through? → A:
  Only "no reply" (unreachable, including a timeout), "reply unreadable" and an API error. "Not
  configured" and "not read yet" (which is what a changed N.I.N.A. address gives) stop the close
  as today: neither is a missed poll of the server the close was started against.
- Q: How long may a close ride through failed reads? → A: Until the last good N.I.N.A. reading is
  older than the freshness limit (`NINA_FRESH_MS`, 10 s), measured by the control task itself at
  every step, not at the main loop's once-a-second evaluation. The same bound now also applies to a
  good reading during the close, so a close never runs on a reading past its freshness, however
  late the loop is.
- Q: What does the close ride on? → A: The last good reading, and only if it, with the park
  switches as read now, would itself confirm the park: connected, at park, not slewing, fresh, and
  agreeing with every fitted switch. Any good reading that does not say parked replaces it at once.
- Q: Which calibration legs and moves does this cover? → A: Everything the park interlock already
  watches: a close, every calibration leg and every jog. The open-leg backstop of the calibration
  run uses the same rule. Starting any of them still needs a fresh, good, parked reading.
- Q: Is this a change to Principle II? → A: A clarification (PATCH 2.1.1). The rule for the start
  ("all fresh at the moment the close starts", unreadable counts as not parked) is unchanged. The
  re-check during the close names its stop triggers (a mount that reports unparked, a fitted
  switch that opens); the clarification says the re-check uses the latest good reading while it is
  fresh, and that evidence past its freshness stops the close. See "Constitution rules".
- Q: How does a page send only what changed? → A: The page compares its form with the settings as
  it loaded them and sends only the fields that differ (a partial document). The device already
  merges a partial body key by key; a missing key keeps its setting.
- Q: How does the device spot a save from an old page? → A: `GET /api/config` returns the
  settings revision in a response header. A save sends the revision its form was loaded from and,
  for each field it changes, the value it was loaded with. If the revision is current, the save
  goes through. If not, the device compares each of those loaded values with its own: any that
  differ refuse the save with 409 and their names. Fields that nobody else changed save normally,
  so a SQMeter sync or another page's change elsewhere never blocks a save.
- Q: What about credentials? → A: A field that belongs with a secret is checked with its partners:
  the Wi-Fi network and password, the MQTT broker, port, user name and password, the ntfy server,
  topic and token, the Pushover keys, the webhook address and header, and the web user name and
  password. Changing one of them from an old page is refused if any partner changed, so a password
  never lands on a network or server it wasn't typed for.
- Q: What happens after a 409? → A: Settings says which settings changed on the device and offers
  Reload. Reload reads the device's settings and puts back every edit the person made except those
  to the changed fields, which now show the device's values. The wizard and the calibration page do
  the same at once and say so.
- Q: What about a save with no revision? → A: It works as today (curl, scripts, an older page). A
  restore keeps its own revision check (spec 015 backup) and replaces everything, as it should.
- Q: Does the revision survive a restart? → A: No, and it doesn't need to: it starts from a random
  value at each start, so a page loaded before a restart never matches a revision after it by
  chance.

## Constitution rules this spec relies on

| Principle | Rule | Where it is kept |
|---|---|---|
| I | Any motion fault stops the motor, drops EN and latches | FR-001 to FR-004: the park interlock still latches; it only stops treating one missed reply as evidence |
| I | Decisions live in `lib/` with native tests for every transition and boundary | FR-005: the hold, the memory and the gate rule are pure and tested |
| II | No close unless N.I.N.A. reports AtPark and every fitted switch is closed, all fresh when it starts | FR-006: the start check is unchanged |
| II | Re-checked throughout the close: a mount that reports unparked, or a switch that opens, stops it as a motion fault | FR-001, FR-002: any such evidence stops it in the same step; FR-003 bounds the ride-through by the freshness limit |
| III | Doubt resolves to unsafe; no source is trusted past its freshness | FR-003: a failed read never extends a reading's life |
| VI | Request handlers don't block; persisted config validated before saving | FR-012: the stale check runs inside the existing save, on the existing lock |
| VIII | Secrets masked and kept when the mask is sent back | FR-011: masks compare as masks; credentials are checked with their partners |
| IX | Docs move with behaviour | FR-016 |

## User Scenarios & Testing *(mandatory)*

### User Story 1 - A close rides through one missed N.I.N.A. reply (Priority: P1)

The roof is closing onto a parked mount. N.I.N.A. misses one poll (a timeout) and answers the next.
The close carries on and the roof ends closed.

**Why this priority**: today the roof stops part-way open with a latched fault, possibly in rain.

**Independent Test**: native tests feed the park inputs step by step; on the bench, block
N.I.N.A.'s port for a few seconds during a close.

**Acceptance Scenarios**:

1. **Given** a close under way with a good, parked reading 2 s old, **When** the next read times
   out, **Then** the close goes on.
2. **Given** that, **When** the next read is good and parked, **Then** the close goes on with no
   fault and nothing latched.
3. **Given** failed reads continue, **When** the last good reading becomes older than 10 s,
   **Then** the motor stops in that control step and the park-interlock fault latches, naming
   N.I.N.A.'s freshness.

---

### User Story 2 - Positive evidence still stops at once (Priority: P1)

During the ride-through, N.I.N.A. answers that the mount is not at park, or slewing, or
disconnected; or a fitted park switch opens or can't be read; or the safety hand-over stalls.

**Why this priority**: constitution II: a close onto an unparked mount is the costliest failure.

**Independent Test**: native tests of each transition.

**Acceptance Scenarios**:

1. **Given** a ride-through, **When** a good reading says not at park, slewing or not connected,
   **Then** the close stops and latches at the next evaluation.
2. **Given** a ride-through, **When** a fitted switch reads open or unreadable, **Then** the close
   stops in that control step.
3. **Given** a ride-through, **When** the main loop has not handed over the inputs for 5 s,
   **Then** the close stops, as today.
4. **Given** N.I.N.A.'s address is changed or removed during the close, **Then** the close stops.

---

### User Story 3 - Starting a close is unchanged (Priority: P1)

**Acceptance Scenarios**:

1. **Given** the latest N.I.N.A. read failed but the one before said parked, **When** anyone asks to
   close (web, Alpaca, MQTT, the automatic close, a waiting close), **Then** it is refused as "mount
   not parked", exactly as today.

---

### User Story 4 - Two pages edit different settings (Priority: P1)

Two browser tabs show Settings. One changes the Wi-Fi network (through Wi-Fi setup) and the N.I.N.A.
address; the other, loaded earlier, turns IPv6 off and saves.

**Why this priority**: the bench lost its N.I.N.A. address and Wi-Fi network this way.

**Independent Test**: Vitest with the mock device; native tests of the stale check; on the bench
with two tabs.

**Acceptance Scenarios**:

1. **Given** the second tab changed only IPv6, **When** it saves, **Then** only IPv6 is sent; the
   new network and N.I.N.A. address stay.
2. **Given** both tabs changed the N.I.N.A. address, **When** the older one saves, **Then** the
   device refuses with 409 naming `park.nina.url`, and the page says the setting changed on the
   device and offers Reload.
3. **Given** Reload, **Then** the page shows the device's settings with the person's other edits
   kept, and says which edits were replaced.
4. **Given** the older tab typed a Wi-Fi password while the network was changed elsewhere, **When**
   it saves, **Then** the save is refused naming `wifi.ssid`.
5. **Given** a script posts a partial document with no revision, **Then** it saves as today.

---

### User Story 5 - A SQMeter sync doesn't block saves (Priority: P2)

The device saves a location or a followed group read from SQMeter (spec 020) while a Settings page
is open. A save of other fields from that page goes through.

**Acceptance Scenarios**:

1. **Given** a sync changed the NTP servers, **When** an open page saves the roof's travel,
   **Then** it saves.
2. **Given** a sync changed the location, **When** an open page saves the location, **Then** it is
   refused, naming the location fields.

### Edge Cases

- A read failure at the exact freshness limit (10 000 ms old) still rides; 10 001 ms stops.
- The last good reading said parked but a switch now disagrees: no ride-through.
- The close starts on a reading 9.9 s old and the next read fails: it stops 0.1 s later.
- The main loop is late: the control task's own clock still stops the close at the limit.
- Restarting the device resets the revision to a random value: an old page's save gets the
  field-level check, never a chance match.
- A masked secret sent back unchanged is never part of the partial document.
- The calibration page saves its proposal as a partial roof section, checked the same way.
- A setting the page normalised (a default it filled in) is never sent unless the person changed it.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: During a close, a calibration leg or a jog, the park interlock MUST stop the motor in
  the same way as today (a latched park-interlock fault) on any of: a good N.I.N.A. reading that is
  not at park, slewing or not connected; N.I.N.A. and the fitted switches disagreeing; a fitted
  switch reading open or unreadable; the safety hand-over older than 5 s; N.I.N.A. not configured or
  not read yet.
- **FR-002**: A failed N.I.N.A. read (unreachable, unreadable, API error) during such a move MUST
  NOT stop it while the last good reading from the same N.I.N.A. address, with the switches as read
  now, would confirm the park and is no older than the freshness limit.
- **FR-003**: The control task MUST check the age of the reading the park relies on (the current
  good one, or the last good one during a ride-through) against the freshness limit at every step,
  with the clock N.I.N.A.'s readings are stamped with. Past it, the interlock stops the move and the
  fault names N.I.N.A.'s freshness.
- **FR-004**: The device's log MUST say when a ride-through starts and ends.
- **FR-005**: The ride-through decision, the last-good memory and the interlock rule MUST live in
  `lib/` with native tests for every transition and boundary.
- **FR-006**: Starting a close, a calibration leg or a jog MUST still need a fresh, good, parked
  reading and every fitted switch closed; a ride-through never confirms a park for a start.
- **FR-007**: The web UI MUST send only the settings the person changed (and the wizard's setup
  state) when it saves, from Settings, the setup wizard and the calibration page.
- **FR-008**: `GET /api/config` MUST return the settings revision (`X-Config-Revision`), read before
  the settings.
- **FR-009**: A save MAY carry `baseRevision` and `baseValues` (the loaded values of the fields it
  changes and of their credential partners). With a current revision it saves. With another one, it
  saves only if every base value equals the device's; otherwise 409 with `conflicts` (dotted
  names, at most 16) and the current `revision`. With a revision but no base values and another
  revision, 409 with no names.
- **FR-010**: A save without `baseRevision` MUST behave exactly as today.
- **FR-011**: The stale check MUST compare against the settings as `GET /api/config` shows them
  (secrets masked), under the same lock as every other settings save, so no save can land between
  the check and the write.
- **FR-012**: The save answer MUST carry the new `revision`.
- **FR-013**: On 409, Settings MUST say the settings changed on the device, name them, and offer
  Reload; Reload keeps every edit except those to the named fields. The wizard and the calibration
  page reload at once and say so.
- **FR-014**: The revision MUST start from a random value at each start.
- **FR-015**: Backup restore, Wi-Fi setup and the SQMeter copy MUST keep working unchanged.
- **FR-016**: Docs: the REST reference, the safety-inputs and close pages, Settings in the user
  guide, the constitution (2.1.1), the roadmap and the changelog.

### Key Entities

- **N.I.N.A. memory**: the last good reading and the N.I.N.A. address generation it came from.
- **Close hold**: parked, riding through, and the time until which the reading it relies on is
  fresh; handed to the control task with the park result.
- **Save base**: the revision a page loaded and the loaded values of what it changes.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A close survives any run of failed N.I.N.A. reads shorter than the freshness limit
  measured from the last good reading (native timeline test).
- **SC-002**: Positive not-parked evidence during a ride-through stops the move within one
  evaluation (1 s) for N.I.N.A., one control step for a switch (native tests).
- **SC-003**: With N.I.N.A. silent, the roof stops no later than 10 s after its last good reading,
  plus one control step (native test; bench check).
- **SC-004**: Two pages editing different fields both save, with neither change lost (Vitest; bench
  check).
- **SC-005**: A stale save of a field another client changed is refused 100 % of the time and names
  the field (native and Vitest).

## Assumptions decided for the owner

Each was an open choice; the safest option the constitution allows was taken.

1. **Ride-through only for missed replies** (unreachable, unreadable, API error). "Not configured"
   and "not read yet" stop a close as before.
2. **The bound is the existing N.I.N.A. freshness limit (10 s)**, measured from the last good
   reading by the control task's clock. No new setting.
3. **The good-reading path is bounded the same way during a move**, so a late main loop can't
   stretch a reading past 10 s (before, up to about 1.25 s more).
4. **The ride-through covers every move the interlock watches** (close, calibration legs, jogs),
   and the calibration run's open-leg backstop uses the same rule. Starts are unchanged.
5. **No new status field or alert**: the latched fault text says why, the log says when a
   ride-through starts and ends.
6. **Principle II gets a PATCH clarification (2.1.1)**, not a MAJOR change: the start rule is
   unchanged, the during-close triggers it names still stop at once, and no reading is used past
   its freshness. The residual risk: if N.I.N.A. stops answering and the mount unparks in the same
   seconds, a roof without park switches keeps closing until the last good reading is 10 s old
   (before: until the first failed reply arrived, which with the poller's 2 s period and 6 s
   request limit was already up to about 9 s after the last good reading). Park switches still
   stop it at once. The owner may prefer to treat this as MAJOR.
7. **Field-level stale check by compare-and-save of loaded values**, not a history on the device:
   no memory kept between saves, and a sync or another page's change elsewhere never blocks a save.
8. **Credential partners are checked together**, so a password can't be saved against a network or
   server changed elsewhere.
9. **After 409 the device's value wins** for the named fields; the person's other edits stay.
10. **The revision travels in a header on GET** (not in the settings document), so a script that
    reads, edits and posts the document back keeps today's behaviour.
11. **A changed secret can't be told from another changed secret** (both are masked); setting or
    clearing one is detected, and its partners are checked with it.
