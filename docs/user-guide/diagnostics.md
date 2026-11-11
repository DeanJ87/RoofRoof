---
description: "Advanced diagnostics: an optional Diagnostics page with the roof's live readings, the control task's timing and the device's memory, plus a log of the last moves with current and supply profiles, CSV and JSON downloads, and optional MQTT. Off by default; it never affects the roof."
---

# Advanced diagnostics

Advanced diagnostics shows what the roof and the device are doing while a move runs, and keeps a short record of the last moves. It helps you commission the roof, see whether the motor or the drive is wearing, and check that the device has room to spare.

It is **off by default**. With it off the device records nothing, allocates nothing, answers `{"enabled": false}` on its diagnostics routes and publishes nothing. Switching it on changes no setting that moves the roof.

!!! info "Readings only"
    Diagnostics never opens, closes or stops the roof, and no decision uses what it records. The only action is **Clear log**, which empties the record.

---

## Switching it on

Open **Settings → Device → Diagnostics**. There are three settings, none needs a restart:

| Setting | Key | Default | What it does |
|---|---|---|---|
| Advanced diagnostics | `diag.enabled` | off | The Diagnostics page, the live stream and the move log |
| Keep move summaries | `diag.keepSummaries` | off | Keeps the last 32 move summaries across restarts. Has no effect while diagnostics is off |
| Diagnostics over MQTT | `diag.mqtt` | off | Publishes the live document and each finished move. Has no effect while diagnostics or MQTT is off |

The same keys are in the [configuration reference](configuration.md#diagnostics).

---

## The Diagnostics page

With diagnostics on, the **System** page has a Diagnostics card with a few live values and a link. The page itself is at `/diagnostics`. It is not in the main navigation. With diagnostics off the page says so and links the setting.

### Live values

The values update four times a second while the roof moves and every two seconds at rest. A value the device can't read shows as not available.

| Group | Figure | What it tells you |
|---|---|---|
| Roof | State, target, position | As on the dashboard |
| | Phase | Where the move is in its ramp: `idle` (not moving), `deadTime` (moving, motor not yet driven), `slow` (a move from an unknown position), `softStart` (speeding up), `cruise` (top duty), `slowing` (slowing for the end) and `creep` (at the slow duty near the end) |
| | Duty | The motor drive as a percentage, with the driver's enable and PWM outputs |
| | Supply | The 12 V supply now |
| | Supply at rest | The average of a full second of readings with the motor off, taken before the move. It is the baseline for the sag |
| | Sag | Supply at rest minus the supply now, while the motor is driven |
| | Motor current, power | Current (with current sense on), and power as volts times amps |
| | Drive budget | The motor's driven time toward the move's target against the most the roof allows. See [Roof motion](roof-motion.md) |
| | Elapsed, remaining, timeout | Time since the move began, the expected time left (while the position is known) and the move's time limit |
| Control task | Step time | How long the last control step took, with the longest, against the 20 ms period |
| | Longest gap, watchdog margin | The longest time between two steps since start, and how far that is from the 100 ms watchdog limit |
| | Watchdog trips | How often the watchdog has fired |
| Main loop | Loop pass | How long one pass of the main loop took, with the longest |
| Memory | Free heap, lowest, largest block | Free memory now, the least it has been, and the biggest single piece |
| | Stack headroom | Spare stack of the control, main loop and network tasks |
| Network | WiFi signal, uptime, reset reason | As on the System page |

Three figures have a healthy range:

- **Step time** should stay far below 20 ms. The recording adds a few microseconds.
- **Stack headroom** should stay above 2 KB for every task.
- **Free heap** should stay above about 40 KB, so an alert's TLS session can open.

In the [demo](../live-demo.md) the device-only values (heap, stacks, step time, loop pass, WiFi signal and reset reason) show as not available.

### The chart

The chart draws the current and the supply (and the duty) against time for the move in progress, or the last one. Each point is the highest current and the lowest supply of its interval, so a short stall or sag stays visible. A text summary under it gives the same figures for screen readers.

### The move log

The device holds the profiles of the last **5 moves** and the summaries of the last **32**. Select a move to see its chart and summary.

A summary has:

- when it started and ended (as clock time when the clock is set), who asked (a person, the automatic close, an imaging app or MQTT) and where it was going;
- the result: `arrived` (the target magnet was reached), `stopped`, or `fault` with the fault and its measured value and limit;
- whether it was a leg of the [calibration run](calibration-run.md), marked "Calibration" in the list;
- the duration and the time the motor was driven, and whether it began from an unknown position;
- the peak and average current, the supply at rest, the lowest supply and the sag;
- the stall current: only for a fault while driving, the average of the last 260 ms of current readings;
- the Hall reading at the start and the end.

A profile point covers 250 ms at first. When the 120 points are full, pairs of points merge and the interval doubles. The whole move always fits in 120 points, however long it takes.

### Downloads and Clear log

Each move downloads as **CSV** or **JSON** (see the [REST API](../api/rest.md#get-apidiagmovesid)). **Clear log** empties the log and the kept summaries. It asks for the password when one is set, and the device refuses it while the roof moves or a close is waiting.

---

## Kept summaries

With **Keep move summaries** on, the device writes the summaries to one file of about 2 KB, so the list survives a restart. Kept moves have no profile and no uptime.

- The file is written at most once every 5 minutes, and only while the roof is at rest, no close is waiting and the close sequence is idle. It never delays a move.
- Switching **Keep move summaries** on part-way through a session starts a new kept list at the next write.
- The file is loaded when diagnostics is switched on and at start-up. A file that doesn't check out is ignored.

---

## MQTT

With diagnostics, **Diagnostics over MQTT** and [MQTT](mqtt.md) on, the device publishes:

| Topic | Payload | When |
|---|---|---|
| `<base>/diag` | The live document | Every 60 seconds at rest |
| `<base>/diag/move` | One move summary | Once for each finished move, once the roof is at rest |

Nothing is published while the roof moves, a close waits or the automatic close is attempting, so diagnostics never takes part of the budget the roof's own state document needs. Neither topic is retained.

---

## Why it can't affect the roof

- **Recording runs after the motor output is applied.** Each control step puts its output on the pins first, then records. The recording is a few additions and comparisons; it allocates nothing, logs nothing and does no input or output.
- **The work is bounded.** The most it does in one step is merge 120 points into 60, a few times in the longest move.
- **Reads are bounded copies.** A page, stream or download takes a copy of about 1 KB under the roof's lock, then formats it. The control task waits for that copy and nothing longer.
- **Nothing feeds back.** No decision of the roof, the automatic close, the alerts or the safety rules reads a diagnostics value.
- **Memory is taken only while it is on.** The log's memory is allocated when you switch diagnostics on and freed when you switch it off. If it can't be allocated, the document says `"log": "unavailable"` and the roof carries on.

---

## Spotting wear

One move says little. Compare moves in the same conditions:

- **Peak and average current** that rise from move to move suggest more friction: a dry runner, a bent rail, a binding wheel or debris.
- **Sag** that grows with the same current suggests a tired battery or a poor connection in the supply wiring.
- **Duration** that creeps up for the same travel suggests the roof is slowing down. Check that the drive isn't running out of its budget.
- A current peak at the same place in each profile points to one spot on the track.

Keep the summaries on, download a CSV now and then, and compare the first move after commissioning with later ones.
