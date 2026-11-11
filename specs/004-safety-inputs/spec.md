# Feature Specification: Safety Inputs

**Feature Branch**: `spec/004-safety-inputs`

**Created**: 2026-11-11

**Status**: Draft

## Context

RoofRoof must close the roof when the weather turns, and must only close onto a parked mount
(constitution II and III). Before anything can act on that (spec 005), the controller needs two
answers it can trust:

- **Is it safe to be open?** From the owner's SQMeter (its HTTP safety document and/or its MQTT
  topics) and an optional local rain contact.
- **Is the mount parked?** From N.I.N.A. (Advanced API plugin) AND, when fitted, the park
  switches.

This spec produces those two answers, each with the reasons behind it, and the N.I.N.A. client
that can also ask the mount to park. It does not act on them: closing, refusing to open and the
alerts are specs 005 and 007. Reading the GPIO pins is spec 003; here the rain contact and park
switch readings are plain input values.

Doubt always resolves to the safe side: a source that is missing, late, unreachable or
unreadable makes the weather "unsafe" and the mount "not parked".

## Clarifications

### Session 2026-11-11

Each open question took the safest option the constitution allows;
each is also listed under "Assumptions decided for the owner".

- Q: Does 004 report the verdicts in `/api/status`, MQTT or the UI? → A: No. The verdicts are
  logged when they change (info level) and exposed to firmware code for spec 005. The status
  document, MQTT topics and the dashboard card are specs 008 and 009, which finalise those
  documents together with the roof's own fields from spec 003. This keeps the document contracts
  changing once, not three times.
- Q: How many park switches can be fitted, and how are they combined? → A: A setting says none,
  one (GPIO32) or two (GPIO32 and GPIO33). Every fitted switch must read closed.
- Q: How long may a reading be old? → A: SQMeter over HTTP: a setting, default 30 s (SQMeter
  evaluates every second and is polled every 5 s). SQMeter over MQTT: fixed at 150 s, because
  SQMeter republishes its retained safe topic every 60 s even when nothing changes. N.I.N.A.: 10 s
  (it is polled every 2 s).
- Q: What does "unknown" mean? → A: Only the start-up window: no enabled source has answered yet
  and the stale time since start has not passed. Unknown is not safe. After that window a silent
  source is "lost", and the verdict is unsafe.
- Q: Plain `http://` only, or also `https://`? → A: `http://` only for SQMeter and N.I.N.A. Neither
  serves TLS (N.I.N.A. v2 has no TLS, SQMeter has none), and it keeps the poller off the shared TLS
  session (constitution VI).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - One weather verdict from every configured source (Priority: P1)

The owner points RoofRoof at SQMeter (by HTTP, MQTT or both) and says whether a rain contact is
fitted. The controller keeps one verdict: safe, unsafe with its reasons, or unknown at start-up.
Any source saying unsafe or rain makes it unsafe; a source that stops answering, answers late or
answers nonsense also makes it unsafe, naming the source and what failed.

**Why this priority**: Constitution III. Spec 005 closes the roof on this verdict, so it must be
right before anything else is built on it.

**Independent Test**: Native tests feed the combination rule every source state (safe, unsafe,
rain, rain sensor fault, stale at the boundary, unreachable, unparseable, offline, not enabled)
and check the verdict and reasons; on a bench, unplugging SQMeter's network turns the logged
verdict unsafe within the stale time.

**Acceptance Scenarios**:

1. **Given** SQMeter over HTTP enabled and answering `safe: true, reasonFlags: 0`, **When** the
   verdict is taken, **Then** it is safe.
2. **Given** any enabled source reporting unsafe, **When** the verdict is taken, **Then** it is
   unsafe and lists that source's reason.
3. **Given** SQMeter reporting rain (reason bit 9) or a failed rain sensor (bit 10), or the rain
   contact reporting rain, **When** the verdict is taken, **Then** it is unsafe with "rain" (or
   "rain sensor fault"), even if every other source is stale or lost.
4. **Given** the last good reply from a source is older than its stale time, **When** the
   verdict is taken, **Then** it is unsafe with "source stale", stating the age and the limit.
   At exactly the stale time it is still fresh; one millisecond more is stale.
5. **Given** replies that can't be read (not JSON, a missing or wrong-typed `safe` or
   `reasonFlags`, an unknown MQTT payload) or can't be fetched, **When** no good reply has arrived
   for the stale time, **Then** that source counts as unsafe, named by its latest failure
   ("unreadable: missing reasonFlags", "unreachable: connection refused"). A single failed poll
   inside the stale time does not change the verdict.
6. **Given** SQMeter's MQTT availability says `offline`, **When** the verdict is taken, **Then**
   the MQTT source counts as unsafe ("offline"), whatever its last retained safe value says.
7. **Given** no weather source enabled at all, **When** the verdict is taken, **Then** it is
   unsafe ("no weather source").
8. **Given** a fresh start and no source has answered yet, **When** the verdict is taken within
   the stale time, **Then** it is unknown (not safe); after the stale time, unsafe ("source
   lost").
9. **Given** SQMeter says `safe: true` but a non-zero `reasonFlags`, **When** the verdict is
   taken, **Then** it is unsafe (the flags win).

---

### User Story 2 - Park confirmed by N.I.N.A. AND the switches (Priority: P1)

The controller decides "parked" only when N.I.N.A. reports the mount connected, at park and not
slewing, with a fresh reading, AND every fitted park switch is closed. Anything else, including
the two sources disagreeing, is "not parked", with the reasons and a flag that the sources
disagree (spec 005 raises the critical alert on it).

**Why this priority**: Constitution II. Closing onto an unparked mount is the most expensive
failure the device can cause.

**Independent Test**: Native tests of the park rule over every combination of N.I.N.A. state and
switch readings, including disagreement and the freshness boundary.

**Acceptance Scenarios**:

1. **Given** N.I.N.A. fresh, connected, `AtPark` true, not slewing, and no switches fitted,
   **When** the rule runs, **Then** parked.
2. **Given** the same and two switches fitted and both closed, **Then** parked; **Given** either
   switch open or unreadable, **Then** not parked, naming the switch.
3. **Given** N.I.N.A. says parked and a fitted switch is open (or N.I.N.A. says not parked and
   every fitted switch is closed), **When** the rule runs, **Then** not parked and "sources
   disagree" is set.
4. **Given** N.I.N.A. not configured, unreachable, unreadable, older than its freshness limit,
   the mount not connected, or slewing, **When** the rule runs, **Then** not parked, naming why.
5. **Given** a reading exactly at the freshness limit, **Then** it is fresh; one millisecond
   older, stale.

---

### User Story 3 - Talk to N.I.N.A.'s Advanced API (Priority: P1)

The controller reads the mount's state (connected, at park, slewing) from N.I.N.A.'s Advanced API
v2 every 2 seconds, and can ask it to park. The protocol is hidden behind an interface so API v3
(different paths, POST, 202 replies, optional Basic auth) can be added later without touching the
rules.

**Why this priority**: Without it there is no park confirmation, and spec 005 can't ask for a
park.

**Independent Test**: Native tests build the v2 requests and parse recorded v2 replies (mount
info connected/parked/slewing/disconnected, park "Parking", "Mount already parked", the 409
"Mount not connected" error, a 500, truncated and non-JSON bodies, missing fields).

**Acceptance Scenarios**:

1. **Given** a v2 mount-info reply with `Success: true` and `Connected`, `AtPark`, `Slewing`,
   **When** parsed, **Then** the three values come out as read.
2. **Given** a reply with `Success: false`, a non-200 status, a body that isn't JSON or a missing
   or wrong-typed field, **When** parsed, **Then** the reading is "unreadable" or "API error" with
   the reason text, never a guess.
3. **Given** a park request, **When** N.I.N.A. answers "Parking" or "Mount already parked",
   **Then** the request counts as accepted; `Success: false` counts as refused, with N.I.N.A.'s
   error text; any other answer counts as unreadable (never as accepted).

---

### User Story 4 - Configure the sources (Priority: P2)

The owner sets which sources are used and where they are: SQMeter's safety URL, SQMeter's MQTT
base topic, the stale time, whether the rain contact is fitted, N.I.N.A.'s address and how many
park switches are fitted. The settings are validated the same way by the device and the web UI,
survive a restart and a backup/restore, and an older saved config loads with every source off.

**Why this priority**: Needed to use the sources at all, but the settings page itself is spec
009; until then the settings are set through the settings API or a restored backup.

**Independent Test**: Native config tests and the shared range fixture; the web schema tests.

**Acceptance Scenarios**:

1. **Given** a config saved by 001, **When** loaded, **Then** it loads, every safety source is
   off, N.I.N.A. is not configured and no switches are fitted.
2. **Given** an `https://` or malformed SQMeter or N.I.N.A. URL, a bad topic or a stale time out
   of range, **When** saved, **Then** both the device and the browser reject it with the same
   message.
3. **Given** the SQMeter MQTT source enabled while MQTT is off, **When** the settings
   dependencies are checked, **Then** it is reported as needing MQTT on.

### Edge Cases

- SQMeter's HTTP reply says `evaluatedAgeMs` older than the stale time: stale (SQMeter itself has
  stopped evaluating).
- MQTT reconnects and the broker hands back an old retained `safe` value while SQMeter is gone:
  the retained `availability` is `offline` (SQMeter's last will), so the source is offline. If
  the broker lost the will (a broker restart with persisted retained messages), the first `safe`
  and `safety` message after each subscription only counts when it says unsafe: a replayed
  "safe" never makes the source fresh, so a dead SQMeter still goes stale.
- MQTT `safe` arrives but `availability` never has: the source is not trusted until it has seen
  `online` (unknown during start-up, lost after it).
- The `<base>/safety` JSON says rain while `<base>/safe` lags: rain wins.
- The rain contact is fitted but spec 003 has not supplied a reading: "rain contact unreadable",
  unsafe.
- N.I.N.A. answers but the mount is disconnected: not parked ("mount not connected").
- N.I.N.A. `AtPark` true while `Slewing` true: not parked.
- The 32-bit millisecond clock wraps after 49 days: times are kept as 64-bit milliseconds since
  start, so a source silent for weeks is still stale.
- A reading timestamped in the future (taken after `now` was sampled): age 0, fresh.

## Requirements *(mandatory)*

### Functional Requirements

**Weather verdict (pure logic)**

- **FR-001**: The verdict MUST be one of safe, unsafe or unknown, with a set of reasons. Only
  "safe" is safe.
- **FR-002**: Each weather source MUST be one of: not enabled, waiting (no reply yet), good (safe),
  good (unsafe with SQMeter's reason flags), stale, unreachable, unreadable, offline (MQTT only).
- **FR-003**: Any enabled source that is unsafe, stale, unreachable, unreadable, offline or lost
  MUST make the verdict unsafe, with a reason naming the source and the failure.
- **FR-004**: SQMeter reason bit 9 (rain) from any SQMeter source, or the rain contact closed,
  MUST give the reason "rain"; bit 10 MUST give "rain sensor fault". Both are unsafe whatever any
  other source says.
- **FR-005**: A SQMeter reply MUST be unsafe if `safe` is false OR `reasonFlags` is non-zero.
- **FR-005a**: A failed or unreadable reply MUST never refresh a source. While the last good reply
  is within the stale time it still stands; after that the source is unsafe, named by its latest
  failure (unreachable, unreadable) or "stale" when nothing failed but nothing arrived.
- **FR-006**: A source is stale when its last good reply is older than its stale time (strictly
  greater). For HTTP the age includes SQMeter's own `evaluatedAgeMs` when it is present.
- **FR-007**: No enabled weather source (HTTP and MQTT both off) MUST give unsafe ("no weather
  source"). The rain contact alone is not a weather source.
- **FR-008**: Before any enabled source has answered and while the time since start is within
  the stale time, an enabled source is "waiting" and the verdict is unknown unless another source
  is unsafe. After that, waiting counts as lost (unsafe).
- **FR-009**: The verdict MUST carry, for every failing source, the measured value and the limit
  or the failed input (constitution III), and a text form for the log
  (e.g. `SQMeter HTTP stale: 75 s > 30 s`).

**SQMeter sources (parsing pure, transport in firmware)**

- **FR-010**: HTTP: GET the configured URL every 5 s with 3 s connect and reply timeouts; the body
  is read until 6 s after the request started and capped at 8 KB. (Header lines each have the 3 s
  timeout and the DNS lookup none, so a broken server can hold a poll longer; its readings then go
  stale, which is unsafe.) A reply is good only if
  the status is 200 and the body is a JSON object with a boolean `safe` and a non-negative integer
  `reasonFlags`; `evaluatedAgeMs`, if present, MUST be a non-negative integer.
- **FR-011**: MQTT: subscribe to `<base>/safe` (`1` or `0`), `<base>/safety` (the same JSON as
  HTTP) and `<base>/availability` (`online` or `offline`) on the device's own broker. The source is
  good only after `online` and while its last safe or safety message is at most 150 s old. Any
  other payload is unreadable.
- **FR-011a**: After each MQTT (re)subscription, the first message on `<base>/safe` and on
  `<base>/safety` MUST only be used when it says unsafe (it is usually the broker's retained copy,
  of unknown age).
  Known limit: an MQTT settings save reconnects from the web server's task (platform code from
  spec 001), and then a retained copy can arrive before the reconnect is noticed and count as
  live. It needs the broker to still say SQMeter is `online`; moving that reconnect onto the main
  loop (a platform change) removes it. Resolved by spec 010 (FR-013): the main loop applies MQTT settings.
- **FR-012**: The rain contact, when fitted, is an input value from spec 003: raining, dry or
  unreadable.

**Park confirmation (pure logic)**

- **FR-013**: Parked MUST require all of: N.I.N.A. configured, its reading good and no older than
  10 s, the mount connected, `AtPark` true, `Slewing` false, and every fitted park switch reading
  closed.
- **FR-014**: When N.I.N.A.'s good reading and the fitted switches point opposite ways (N.I.N.A.
  at park and a switch open; or N.I.N.A. connected and not at park while every fitted switch is
  closed), the result MUST be not parked with "sources disagree" set.
- **FR-015**: The park result MUST list each reason it is not parked (N.I.N.A. not configured,
  unreachable, unreadable, API error, stale with age and limit, mount not connected, not at park,
  slewing, switch 1 or 2 open or unreadable, sources disagree).

**N.I.N.A. Advanced API client**

- **FR-016**: The protocol MUST sit behind an interface that builds the "mount info" and "park"
  requests and parses their replies; v2 is the only implementation now.
- **FR-017**: v2: info is `GET /v2/api/equipment/mount/info`, park is
  `GET /v2/api/equipment/mount/park`, both on the configured base URL, no authentication. Replies
  use the envelope `{Response, Error, StatusCode, Success, Type}`.
- **FR-018**: The firmware MUST poll mount info every 2 s with a 3 s timeout on its own task, never
  on the web server's or the motion task, and MUST offer a park request to later specs.

**Settings**

- **FR-019**: New settings, all off or empty by default: SQMeter HTTP on/off and URL, SQMeter MQTT
  on/off and base topic, stale time (10-600 s, default 30), rain contact fitted, N.I.N.A. base URL
  and API version (v2), park switches (none, one, two).
- **FR-020**: Device and web UI MUST apply the same validation: URLs `http://` only, the topic rule
  already used for MQTT, the stale range from the shared fixture.
- **FR-021**: A config saved before this spec MUST load without a reset and with every new source
  off (constitution: a migration never turns a feature on).
- **FR-022**: The SQMeter MQTT source MUST be listed in the settings dependencies as needing MQTT
  on (and the broker connected).

**Reporting**

- **FR-023**: The firmware MUST log the weather verdict and the park result when either changes
  (info level), with the reason text. It MUST NOT log secrets.

### Principle I-III rules this spec relies on, and the tests that prove them

| Rule | Where | Test (native unless stated) |
|---|---|---|
| III: any source unsafe → unsafe | FR-003 | `test_safety_input_logic`: `test_unsafe_when_any_source_unsafe_*` |
| III: missing, stale, unparseable → unsafe | FR-003, FR-006, FR-008 | `test_unsafe_when_http_stale_by_one_ms`, `test_unsafe_when_source_unreadable`, `test_unsafe_when_waiting_after_grace` |
| III: rain acted on even if every other input stale | FR-004 | `test_rain_reason_kept_when_other_sources_stale` |
| III: stale after any silence, however long | FR-006 | `test_stale_after_very_long_silence`; `test_park_logic`: `test_not_parked_after_very_long_silence` |
| III: a retained "safe" replay never refreshes MQTT | FR-011a | `test_sqmeter_source`: `test_retained_safe_copy_never_counts` |
| III: rain sensor fault → unsafe | FR-004 | `test_rain_sensor_fault_bit10_unsafe` |
| III: reasons state value and limit | FR-009 | `test_describe_stale_states_age_and_limit` |
| II: AtPark AND switches, fresh | FR-013 | `test_park_logic`: `test_parked_only_when_all_true`, `test_not_parked_when_nina_stale_by_one_ms` |
| II: disagreement → not parked | FR-014 | `test_disagree_*` |
| II: unreadable → not parked | FR-013 | `test_not_parked_when_switch_unreadable`, `test_nina`: `test_parse_info_*_unreadable` |

### Key Entities

- **Weather source reading**: which source, its state, SQMeter's reason flags, when it was last
  good, and the failure detail.
- **Weather verdict**: safe / unsafe / unknown, a reason set, and the per-source details.
- **Mount reading**: N.I.N.A.'s answer (connected, at park, slewing) or why there is none, and
  when it was taken.
- **Park switch readings**: per fitted switch, closed, open or unreadable.
- **Park result**: parked or not, the reason set, and whether the sources disagree.
- **Safety settings**: described in FR-019.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Every rule in the table above has a native test that fails if the rule is broken;
  `pio test -e native` passes.
- **SC-002**: With SQMeter switched off or unplugged, the logged verdict becomes unsafe within the
  stale time plus one poll (35 s with the defaults over HTTP, 150 s over MQTT, or at once on an
  MQTT `offline`).
- **SC-003**: With N.I.N.A. closed, the logged park result becomes "not parked" within 10 s.
- **SC-004**: No combination of inputs in the test matrix yields "safe" or "parked" while any
  configured source is missing, stale, unreadable or disagreeing (exhaustive native test).
- **SC-005**: Firmware stays within budget (image < 75% of the OTA slot), with zero warnings, and
  the quickstart §1 suite passes.

## Assumptions

- SQMeter's contract is its spec 013 and the schema in SQMeter spec 016 (`safety.schema.json`);
  RoofRoof reads only `safe`, `reasonFlags` and `evaluatedAgeMs`, and treats any other field as
  informational. See [research.md](research.md).
- N.I.N.A. Advanced API 2.2.15 or newer (the mount-info fix is in 2.2.15.0).
- The SQMeter MQTT topics are on the same broker RoofRoof already uses.

## Assumptions decided for the owner

Each was an open choice; the safest option the constitution allows was taken. Review and change
any of them by editing the settings defaults or this spec.

1. **No status document, MQTT or UI output in 004.** Verdicts are logged and kept for spec 005;
   spec 008 adds them to `/api/status` and MQTT, spec 009 to the dashboard and settings page.
2. **Park switches: none / one (GPIO32) / two (GPIO32 + GPIO33)**, every fitted switch must be
   closed. Default none (they are optional, an owner decision).
3. **Stale times**: SQMeter HTTP default 30 s (setting, 10-600 s); SQMeter MQTT fixed at 150 s
   (2.5 × SQMeter's fixed 60 s republish); N.I.N.A. fixed at 10 s (5 polls).
4. **Poll periods**: SQMeter HTTP every 5 s, N.I.N.A. every 2 s, each with 3 s connect and
   reply timeouts; the body is read until 6 s (see FR-010). Not settings.
5. **Unknown** only during the start-up window (see FR-008); after it, a silent source is lost
   and the verdict is unsafe.
6. **No weather source configured is unsafe**, not unknown. A new device therefore reports
   unsafe until SQMeter is set up; that is the fail-safe default.
7. **`http://` only** for SQMeter and N.I.N.A.
8. **SQMeter `safe: true` with a non-zero `reasonFlags` is unsafe.**
9. **MQTT source requires SQMeter's availability `online`**, so a stale retained value from a
   dead SQMeter is never trusted.
10. **N.I.N.A. "slewing" is not parked** even if `AtPark` is true, and a disconnected mount is
    not parked.
11. **v2 park's "Mount already parked" counts as accepted.** The park still has to be confirmed by
    the park rule; acceptance alone never means parked.
12. **The rain contact alone is not a weather source**: it can only make the verdict unsafe.
13. **One failed poll doesn't trip the verdict**: the last good reply stands until the stale time
    passes. Tripping on every dropped packet would close the roof on a weak link; the stale time
    bounds the doubt, as constitution III requires. The park rule is stricter: it uses only the
    latest N.I.N.A. attempt.
14. **Rain stays a reason after its source goes stale**: the last good SQMeter reply said rain, and
    nothing fresher has said otherwise.
