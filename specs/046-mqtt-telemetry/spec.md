# Feature Specification: MQTT Telemetry for Long-Term Logging

**Feature Branch**: `spec/046-mqtt-telemetry`

**Created**: 2026-11-26

**Status**: Draft

## Context

The owner wants a long-term record of the roof in InfluxDB, through Telegraf subscribing to MQTT
(bucket `roofroof`; the broker and Telegraf are set up outside this repository). Today the roof's
MQTT documents are either the retained state document (spec 008/032: every 3 s while moving, built
for Home Assistant, about 2.5 KB) or diagnostics (spec 017: only at rest, and only with
**Diagnostics over MQTT** on). Neither suits a time series: the state document is large and its
cadence is tuned for Home Assistant, and diagnostics never publishes while the roof moves.

What is worth logging differs by state. While the roof moves, position, Hall reading, supply and
duty change every second and are what explains a slow or faulted move. At rest nothing changes for
hours except the supply voltage, which is worth a sample a minute (a battery or a failing supply
shows over days); duty or position at 0 % for 10 hours is noise.

## User Scenarios & Testing

### User Story 1 - A move is logged second by second (P1)

1. Given telemetry on and MQTT connected, when a move starts, `<base>/telemetry` gets a sample at
   the next state check (100 ms), then one about every second, and for 3 s after the roof stops.
2. Each sample is `{"ts","state","pos","hall","volts","duty","phase","target","origin"}`; `pos` and
   `volts` are `null` when unknown or unreadable.

### User Story 2 - The supply is logged at rest, sparsely (P1)

1. At rest, only `{"ts","volts"}`, every `mqtt.telemetry.restVoltsSeconds` (60 s by default).
2. A change of 0.2 V or more since the last sample goes out sooner, but never sooner than 10 s
   after the last sample (a noisy reading can't flood the broker).
3. `restVoltsSeconds` 0: nothing at rest.

### User Story 3 - Every move has one record (P1)

1. With telemetry and advanced diagnostics on, each finished move publishes exactly one record on
   `<base>/move`, from the diagnostics move log: origin, target, end state, result, fault kind,
   duration, driven time, Hall start, end, peak and its time, supply at rest, minimum and sag, peak
   and average current (only with current sense), from unknown, blind, calibration, and `ts` (the
   move's end) and `id`.
2. A move that finishes while MQTT is down has no record; nothing is queued.

### Edge cases

- The clock isn't set: `ts` is the uptime in milliseconds (below 10^12).
- MQTT drops during a move: the samples meanwhile are dropped; nothing is replayed.
- A new move starts within a second of the last one finishing: the finished one's record is
  dropped (it stays in `/api/diag/moves`).
- Diagnostics turned off: no move records; the 1 Hz telemetry carries on.

## Requirements

- **FR-001**: While the roof is opening or closing and for `TAIL_MS` (3 s) after, `<base>/telemetry`
  MUST be published at the first check of a move and then at most once every `MOVING_INTERVAL_MS`
  (1 s).
- **FR-002**: At rest it MUST carry only `ts` and `volts`, every `restVoltsSeconds`, or on a change of
  `VOLTS_CHANGE` (0.2 V) or more (readable to unreadable counts) at least `VOLTS_CHANGE_MIN_MS` (10 s)
  after the last; nothing at rest when `restVoltsSeconds` is 0.
- **FR-003**: One `<base>/move` record per finished move, with the fields of US3, never twice, none
  for a move that finished on an earlier MQTT connection.
- **FR-004**: Neither topic retained; QoS 0; a document that can't be sent is dropped, never queued
  or retried.
- **FR-005**: Heap safety (specs 031/035/038): the documents are written with `snprintf` into fixed
  stack buffers (256 and 512 bytes) and published from them; no `DynamicJsonDocument`, no
  `std::string` growth, no static buffers, nothing from an ISR or the control task.
- **FR-006**: Spec 032's budget: published on the main loop from the state check's snapshot, after the
  state document, and only when a whole busy write budget is left in the pass (`mayWrite`). The
  state document, Home Assistant's entities and discovery are unchanged.
- **FR-007**: Settings `mqtt.telemetry.enabled` (default `true`) and `mqtt.telemetry.restVoltsSeconds`
  (default 60, 0-3600, whole seconds), validated on the device and in the UI, in and out of the
  settings JSON; off publishes nothing.

## Success Criteria

- **SC-001**: A simulated 57 s move checked every 100 ms publishes 57 samples during it and 3 in the
  tail; an hour at rest publishes 60 supply samples.
- **SC-002**: Native tests cover the moving and rest cadence, the 0.2 V rule, one record per move with
  its fields, nothing when off, and every document at its longest inside its buffer.
- **SC-003**: `heap_budget`, `stack_depth`, the LittleFS limit (SIZE-02) and quality stay green.

## Assumptions decided for the owner

- **Move records need advanced diagnostics on** (`diag.enabled`): the record is the diagnostics move
  log's summary, and that log (about 5 KB) is only allocated while diagnostics are on. Keeping a
  second recorder for telemetry would cost heap the budget doesn't have. **Diagnostics over MQTT**
  isn't needed.
- 10 s minimum between rest samples on a voltage change, so ADC noise across the 0.2 V step can't
  publish every check.
- Moving samples go out while the roof is busy, inside spec 032's write budget (a ~200-byte write).
- `ts` of a move record is the move's end, not the publish time.
- The string fields keep the device's machine names (`stateName`, `endName`, `originName`,
  `resultName`, `faultName`, the close phase), the same as the status and diagnostics documents.
- A Hall reading is integer counts; `pos` one decimal; volts and amps two decimals.
