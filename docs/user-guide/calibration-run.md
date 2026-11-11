---
description: "The calibration run: an attended run that opens and closes the roof slowly, times it, reads the Hall sensor at each end and in between, and proposes travel, speed, timeout margin and Hall thresholds for you to accept."
---

# Calibration run

The roof's real numbers (the travel and speed behind every timeout, the timeout margin, which end's
magnet reads high and the two Hall thresholds) are settings. Instead of timing runs with a
stopwatch and reading the Hall sensor by hand, the controller can measure them: you start an
**attended calibration run**, it opens and closes the roof slowly, and it **proposes** the values,
each beside the one saved now. Nothing changes until you accept.

Open it from **Settings → Roof → Calibration run → Open the calibration page**, or from the setup
wizard's **Calibration run** page.

!!! danger "Only with you at the roof"
    The run moves the roof with only its time limit and the magnets to stop it. Start it only when
    you are at the roof, can see it, and can reach **Stop** and the 12 V switch. The mount must be
    parked: the run closes the roof.

## Before the first run

- The motor's direction is right: a first short **Open** from the closed end moves the roof toward
  open ([commissioning stage 8](../hardware/commissioning.md#stage-8-first-attended-moves-on-the-real-roof)).
- The Hall thresholds already see both magnets. The sensor only sees the ends, so the run stops each
  leg where the current thresholds say a magnet is. Set rough thresholds from the live reading
  first ([Calibration](../hardware/calibration.md#hall-sensor)); the run then checks and tightens
  them. A run that can't see a magnet stops at its time limit with a `timeout` fault.
- Measure the travel between the two magnets' trigger points with a tape. The sensor can't measure
  distance, so the run proposes the speed that fits the travel you give.

## Starting it

The page lists what the run needs and whether each holds now: the weather safe (and daylight not
wanting the roof closed), the mount parked, no fault, the automatic close idle, the roof at rest,
the supply above its minimum, a settled Hall reading. Then:

1. Turn on **I am at the roof and can see it**.
2. Say **where the roof is now**: closed, open or part-way. Look at it: the run can't tell the ends
   apart by the polarity setting, which it is measuring. The device checks your answer against the
   Hall reading (a magnet at an end, none part-way).
3. Check the **measured travel**.
4. Optionally **Also run at the top duty**.
5. **Start calibration**.

The device checks everything again and refuses with the reason if anything fails: the weather or
daylight, the park, the automatic close, a fault, a move or a waiting close, an upload, a restart,
the setup hotspot, a low supply, an unsettled Hall reading.

## What it does

| Leg | Duty | Bounded by |
|---|---|---|
| First close (only if the roof isn't closed) | slow | the time limit (`roof.calibrationLimitSeconds`, 180 s) |
| Slow open | slow | the time limit |
| Slow close | slow | 1.5 × the slow open's time + 5 s |
| Top-duty open (if asked) | ramps up to the top duty, back to the slow duty before the end | 1.25 × the slow open + 5 s |
| Top-duty close (if asked) | the same | 1.25 × the slow close + 5 s |

Each leg stops at the **first magnet** after it leaves the one it started on, at its time limit
(a `timeout` fault), or on any of the controller's usual faults: not leaving its start magnet in
the leave time, the drive budget, over-current or a lost current reading (with current sense), a
low supply, a Hall reading at a rail, a late control step, the watchdog, the park interlock. A
fault latches and needs a person to clear it, as always. Above the slow duty the roof only moves
from a magnet.

Between legs the roof rests at each end for a second while the Hall reading is taken. The run
always ends at the closed end.

While the run is under way:

- **Stop calibration** on the page, and the **Stop** bar on every page, stop the roof at once and
  end the run. Any stop ends it.
- Open and close from the web UI, an imaging app or MQTT are refused (`calibrating`).
- Settings saves, uploads, restarts and WiFi changes wait, as while the roof moves.
- If the weather stops being safe (or daylight wants the roof closed), an opening leg stops at
  once, a closing leg finishes its close, and the run ends. The automatic close always wins: it
  ends the run and closes as usual.
- If the mount stops being confirmed parked, the run ends; a closing leg trips the park interlock
  as any close does.
- After a restart or power cut, no run is active and nothing moves; the roof is unknown between
  the magnets as after any restart.

## The result

The page shows each leg's time and the speed it implies at 100 % duty (and the running current
with current sense), the in-between Hall reading and its noise, and each end's reading at rest.
For a finished run it proposes:

| Setting | How |
|---|---|
| Travel | the travel you entered |
| Speed | travel ÷ the duty-weighted time of the top-duty legs (the slow legs without them), to 0.1 mm/s |
| Timeout margin | the largest of 5 s, 20 % of a full move, and 1.5 × the most a leg ran over what the proposed speed predicts + 3 s |
| Which end reads high | the end whose reading at rest is above the in-between level |
| Hall thresholds | each kept if it is clear of the in-between readings (100 counts or 4 × the noise) and read at rest at its end; otherwise moved only as far as needed, never past where the roof stopped |

A value isn't proposed, with the reason, when:

- the open and close legs disagree by more than 30 % (speed and margin): something slipped, or a
  magnet was read early;
- both ends read on the same side of the in-between level (polarity and thresholds): the magnets
  face the sensor with the same pole;
- an end reads less than 300 counts from the in-between level: its magnet is too weak or too far
  from the sensor;
- no threshold is both clear of the noise and read at rest.

A peak running current at 80 % of `roof.maxCurrentAmps` is pointed out; current limits are never
proposed.

**Accept and save** saves the proposed values through the same settings save as Settings (checked
by the web UI and the device). **Discard** changes nothing. A stopped or failed run proposes
nothing. The result is kept until the next run or a restart (`GET /api/roof/calibration`,
[REST API](../api/rest.md#get-apiroofcalibration)).
