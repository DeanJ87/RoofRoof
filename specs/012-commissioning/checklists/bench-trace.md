# Trace: bench checks and owner assumptions to the guide

Every "Bench check (pending)" item of the merged PRs, and where the guide covers it. `S.N` is stage S,
step N of `docs/hardware/commissioning.md` (box S.N on the checklist). "Adv" is the guide's advanced
section (analyser or debug build, not part of the gate).

## PR #3 (spec 002, roof motion logic)

| Item | Step |
|---|---|
| Speed at 100% duty; proportionality, especially at 40% | 8.5 |
| The slow duty moves the roof under load, both directions | 8.2 |
| Hall readings at rest and at each end; the rails only with the sensor disconnected or shorted | 3.1, 3.2, 3.5 |
| A full open and close end at the magnets within the timeout, default margin | 8.4 |
| Leave-time fault with the motor wired backwards | 6.5 (simulated), 8.2 (real) |
| The supply dip at a motor start is under 250 ms | 8.6 |
| Current sense: start-up current vs 250 ms; stall current | 8.8 (over-current tested by lowering the limit, no stall) |
| A jam mid-travel stopped at the timeout; motor and IBT-2 tolerate it | 8.7 (shortened margin, temperature check) |

## PR #4 (spec 004, safety inputs)

| Item | Step |
|---|---|
| `Weather unknown` for at most one poll, then `Weather safe` | 4.1 |
| SQMeter unsafe within 5 s; unplugged within 35 s | 4.2 |
| MQTT source: last will at once; a retained `safe=1` alone not safe | 4.4 |
| N.I.N.A. 2.2.15+: parked, closing N.I.N.A. in about 4 s, slewing and unparked | 4.6 |
| Capture real v2 replies and compare with the fixtures | Adv |
| A wrong URL path is unreadable, no reboot | 4.3 |
| Poller stack low-water above 2 KB | Adv |
| An unrelated settings save doesn't reset the verdict or park | 4.8 |

## PR #5 (spec 003, roof hardware)

| Item | Step |
|---|---|
| Boot between the magnets: unknown, EN low; at a magnet that end | 2.4, 3.4 |
| RPWM and LPWM never together, EN only while driven, 20 kHz | 6.2, Adv |
| Hall counts at rest and each magnet; disconnected sensor reads 0 and `hallRail` | 3.1, 3.5 |
| `supplyVolts` within 0.2 V; divider disconnected reads 0 and open and close refused | 2.5, 6.9 |
| Park switches and rain contact over the real cable; external pull-up | 2.6 |
| SC-002 500 ms block, EN falls within 110 ms, cause `watchdog` | Adv |
| SC-003 a latched fault survives 5 power cycles, clears only by `clear-fault` | 6.6 |
| SC-004 stop drops EN before the REST reply; start waits 500 ms | 6.3, Adv |
| Open refused with no source; close refused with N.I.N.A. off or a switch open; a switch opened mid-close stops with `parkInterlock` | 6.1 |
| Current sense calibration against a clamp meter; surge under 250 ms | 8.8 |
| `maxStepGapMs` near 20 ms under a burst of loads; saves, uploads and restarts answer 409 during a move | 6.7, 11.4, Adv |
| `roof.motorAmps` at 40% and 100%; unplugging IS latches `currentUnreadable` | 8.8 |
| A close during a web upload stops the upload; pulled cable doesn't block a close more than 5 s | 7.11 |
| `minRunningAmps` from a measurement with one sense wire unplugged | 8.8 |
| `maxWatchdogBeatAgeMs` well under 50 over a long session | 11.4 |
| `stackFree.roof` above 2 KB | 11.4 |
| The first move on the real roof is attended | Safety rules, 8 |

## PR #6 (spec 005, close on unsafe)

| Item | Step |
|---|---|
| Park request in 1 s; close 4 to 5 s after parked; `Roof closed after unsafe weather` | 7.1, 8.11 |
| N.I.N.A. closed: "Mount didn't park" at the timeout; no request for 5 minutes | 7.2 |
| Stop during the automatic close: stops, "Automatic close stopped" | 7.3 |
| Stop in the 4 s before the close: no close | 7.3 |
| Stand down, then restart: still stood down, "Still:" alert | 7.4 |
| Restart between the magnets while unsafe: no park request, alert after 2 s | 7.5 |
| Flapping verdict with N.I.N.A. closed | 7.6 |
| Alerts master switch off, one channel on: roof alerts arrive | 5.4, 7.2, 7.7 |
| Main-loop stack and heap after a full sequence | Adv |
| First run on the real roof attended | 8.11 |

## PR #7 (spec 007, roof alerts)

| Item | Step |
|---|---|
| Seven roof rows on a phone; critical ones Urgent/Wake me only, editable with Send alerts off; owner review | 5.2, 5.4 |
| Test on each roof row arrives with example values | 5.2 |
| Alerts off, unsafe, N.I.N.A. closed: "Mount didn't park" arrives | 7.2 |
| Alerts on, unsafe: "Unsafe: closing" then "Roof closed after unsafe weather" | 7.1 |
| Hall disconnected during a move: "Roof fault" | 6.8 |
| Supply below `roof.minSupplyVolts` for 30 s | 6.9 (by raising the limit) |
| SQMeter stopped past stale + 30 s: "Weather source lost" | 6.10 |
| Reset with a latched fault: no repeat; after a power cut one alert after 60 s | 6.12 |
| A critical alert over MQTT only carries the reason | 5.5 |

## PR #8 (spec 006, Alpaca Dome)

| Item | Step |
|---|---|
| N.I.N.A. discovers, connects, opens and closes; ShutterStatus sequence | 9.1, 9.2 |
| Unsafe: Open shows the weather reason | 9.3 |
| Unparked: Close shows the park reason | 9.4 |
| N.I.N.A.'s Stop in safe weather; in an unsafe close an error and the close continues; web Stop still works | 9.5 |
| Glue: a client's stop doesn't stand the sequence down; refused behind an upload in bad weather | 9.5 |
| After a boot between the magnets, Open and Close say only a person can | 9.6 |
| Alpaca off: every Dome command answers "disabled" | 9.7 |
| ConformU against the real device only in bench mode or attended | 9.8 |
| Heap and stack headroom with N.I.N.A. polling | Adv |

## PR #9 (spec 010, network under motion)

| Item | Step |
|---|---|
| 200 rounds of save and close/stop: `maxStepGapMs` under 100, no trips | Adv |
| Restart, uploads, WiFi connect answer 409 during the waiting close | 7.8 |
| After Stop, restart and WiFi change accepted | 7.8 |
| Router off: accepted from the hotspot | 7.8 |
| Hotspot on: open answers 409 `hotspot`; stop and close as usual | 7.9 |
| WiFi change with the roof open: a close waits, starts within 15 s | 7.10 |
| Saving MQTT settings with the broker unreachable answers within 1 s | 7.10 |
| Command-line OTA (roof closed) restarts through the plan; open refused | 7.12 |
| Power pulled mid-move: reset reason logged, unknown, no motion | 6.11, 8.9 |
| A fault followed at once by a restart: waits until saved; still latched | 6.6 |

## PRs #10 (008), 009 and 011 (in progress when this was written)

Their bench lists are added to this file, and to the guide, when those PRs merge. Steps that depend
on them: stage 10 (MQTT and Home Assistant, spec 008), the commands in the guide's shell functions
(the dashboard's roof card, spec 009), the Roof settings tab's fields (spec 009), bench mode as a stand-in for the
hand-held magnet (spec 011).

## Owner assumptions placed in the guide

| Spec | Assumption | Step |
|---|---|---|
| 002 | Stop keeps the estimate; a move from `stopped` carries on | 8.3 |
| 002 | A rail reading latches a fault even at rest | 3.5 |
| 002 | Supply low at rest refuses; during a move it faults | 6.9, 8.6 |
| 002 | Speed taken as proportional to duty; the margin covers the difference | 8.5 |
| 002 | The default thresholds fail safe ("no magnet before the timeout") | 3.3, 6.4 |
| 003 | Close needs a confirmed park, open a safe verdict; a new device refuses both until set up | 4, 6.1 |
| 003 | Park-switch polarity fixed; three equal readings; unreadable after a second of chatter | 1.5, 2.6 |
| 003 | Settings saves refused while moving; REST commands count as a person's; stop needs the password | 2.3, 6.7 |
| 003 | A watchdog trip survives a crash; a damaged fault record latches a fault | 6.6 |
| 004 | Park switches none/one/two, default none; no source is unsafe; `http://` only | 2.6, 4 |
| 004 | One failed poll doesn't trip the verdict; stale time bounds it | 4.2 |
| 004 | "Mount already parked" counts as accepted; slewing is not parked | 4.6 |
| 005 | A person's stop stands the sequence down (10 minutes of safe weather or closed) | 7.3, 7.4 |
| 005 | `park.timeoutSeconds` default 240 s, set above the mount's longest park | 7 (setup), 11.2 |
| 005 | Unknown position or fault: nothing sent; one critical alert after 2 s | 7.5 |
| 005 | Before the roof runs unattended at least one alert channel must be set up | 5, 11.1 |
| 005 | A stand-down survives a reset, not a power cut | 7.4 |
| 006 | An Alpaca client is not a person; AbortSlew is Stop; no client stop of a close in bad weather | 9.5, 9.6 |
| 006 | Alpaca off by default | 9.1, 9.7 |
| 007 | Critical alerts can't go below urgent, ignore pause and master switch; need a channel | 5.2, 5.4 |
| 007 | A condition at start-up announced after the grace; a reset loop doesn't repeat | 6.12 |
| 010 | Opening refused while the hotspot is on | 7.9 |
| 010 | Uploads, restarts, WiFi changes refused while the automatic close works; after a stand-down accepted | 7.8 |
| 010 | Reset reason logged | 6.11 |
