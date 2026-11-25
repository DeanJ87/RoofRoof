# Feature Specification: The Roof's MQTT State While It Moves

**Feature Branch**: `spec/032-mqtt-moving-state`

**Created**: 2026-11-25

**Status**: Draft

## Context

Home Assistant's history shows the retained `<base>/state` document going straight from `closed` to
`open` and back, with nothing in between: an automation that runs on every state message ran only
at the end of each move and on the periodic publish at rest, never during a 60 s close. Anything
that follows the roof live from MQTT (a dashboard, an LED strip animated from the start position and
the travel time) needs the move's start as soon as it happens and its progress after that.

The state document's schedule (spec 008, research R4) was built to keep MQTT writes off the main
loop while the roof is busy. Native tests reproduce three faults in it:

1. **The start waits.** A move was published only on a change of state, and only `BUSY_MIN_MS`
   (5 s) after the previous publish. The periodic publish at rest, or since spec 027 the park wait's
   publish just before every move, is usually less than 5 s old when the motor starts, so the start
   went out up to 5 s late, and nothing after it until the end (at most two publishes per busy spell).
2. **One failure silenced every later move.** A publish that failed set a flag that only a new
   connection cleared: a single failure (at rest too, for example a document pool that couldn't be
   allocated or a refused publish that left the connection up) meant no state document while busy
   for the rest of that connection, however many moves followed. The end of the move still went out
   (at rest the flag is ignored), which is exactly what Home Assistant saw.
3. **A write could start with almost no time left.** While busy, MQTT's writes share 500 ms per
   main-loop pass, 250 ms each. A state publish that started late in a pass got only what was left;
   cut short, it drops the connection, and the connection isn't reopened until the roof is at rest.
   The reconnect then publishes the end state, again what Home Assistant saw.

The document's size was checked too: the largest moving document (every text at its longest and
escaped, every dashboard field set, a 128-character base topic) is about 2.5 KB, inside PubSubClient's
3 KB buffer, and spec 031 now streams it from its pool. Size is not a cause; a test keeps it so.

## User Scenarios & Testing

### User Story 1 - A move is visible on MQTT as it happens (P1)

Home Assistant (or any MQTT client) sees `opening` or `closing` within a second of the motor
starting, with `driving`, `target` and the starting `positionPercent`; then the position every few
seconds; then the end state.

**Acceptance**:

1. Given the roof at rest just published, when a move starts, the retained state document goes out
   at the next state check (100 ms), not held to any gap.
2. While it moves, a publish at least every 3 s, with the position, for the whole move.
3. A reversal or a fault mid-move goes out 1 s after the last publish at most; the end (stopped, at
   an end, error) 1 s after the last publish, whatever the at-rest change gap is set to.
4. Home Assistant's Roof cover shows opening and closing (its value template already passes them).

### User Story 2 - A failure never silences later documents (P1)

1. A failed publish while busy is tried again 5 s later; a successful publish clears the failure.
2. A failure (at rest or busy) never delays the end of a move, the rest documents or a later move's
   start on the same connection.

### Edge cases

- Inputs flapping every 100 ms during a close (state, phase, weather, park): at most one publish a
  second while moving, so each write's 250 ms budget bounds the main loop's share.
- A close waiting with the roof still (the automatic close's park wait or retry wait): unchanged,
  only its start and end, at most twice, 5 s apart.
- Not enough of the pass left for a whole busy write: the publish waits for the next check instead
  of starting and dropping the connection.

## Requirements

- **FR-001**: The state document MUST be published at the first state check after a move starts.
- **FR-002**: While the roof is opening or closing, it MUST be published every `MOVING_PROGRESS_MS`
  (3 s) and on a change of state, fault or waiting close `CHANGE_MIN_MS` (1 s) after the last.
- **FR-003**: The first publish after a move or busy wait ends MUST go out `CHANGE_MIN_MS` after
  the last publish, not held to the at-rest change gap.
- **FR-004**: A failed publish MUST NOT stop later publishes: busy, the next try waits `BUSY_MIN_MS`;
  any success clears it.
- **FR-005**: While busy, a state publish MUST only start with a whole `BUSY_WRITE_BUDGET_MS` left in
  the pass.
- **FR-006**: The largest moving document MUST fit PubSubClient's buffer with the topic and header
  (native test).
- **FR-007**: Every write while busy stays held to the spec 008 budgets (250 ms a write, 500 ms a
  pass, a write that can't finish drops the connection, no reconnect while busy).

## Success Criteria

- **SC-001**: A simulated 57 s close checked every 100 ms publishes at the first check of the move,
  never more than 3 s apart while moving, and the end within 1 s.
- **SC-002**: The native tests above fail on the previous schedule and pass on the new one.
- **SC-003**: No web, LittleFS or settings change; flash growth under 1 KB.

## Assumptions decided for the owner

- Progress every 3 s while moving (within the asked 2 to 5 s): about 19 publishes in a 57 s move,
  each one bounded by the 250 ms write budget. Not a setting; the at-rest change gap and interval
  settings are unchanged.
- The busy-but-still schedule (a close waiting for the park or a retry) keeps spec 008's limits:
  nothing in it needs live progress, and it can last minutes.
- A failure while busy is retried after 5 s rather than never: a failure that drops the connection
  already stops all writes until the roof is at rest (no reconnect while busy), so a retry only
  happens when the link stayed up.
- No Home Assistant discovery change: the cover's value template already maps `opening` and
  `closing`; adding a position topic to the cover is left out to keep discovery unchanged.
