---
description: "Printable commissioning checklist for a RoofRoof roof controller: every check of the commissioning guide in order, with a box and a space for the measured value."
---

# Commissioning checklist

Print this page (the browser's print view drops the menus) and tick each box as you go. The step numbers are those of the [commissioning guide](commissioning.md), which says how to do each check. Go in order and **stop at a failed box**.

| | |
|---|---|
| Roof / site | |
| Date and who | |
| Firmware version | |

!!! danger "Order matters"
    Motor **disconnected** through stage 7. Nobody leaves the roof running unattended until stage 11 is ticked: an alert has reached your phone, and the close-on-unsafe dry run has passed, first with the motor disconnected and then attended on the real roof.

## Stage 1: Wiring and the EN pull-down (everything off, motor leads off)

| | Check | Result |
|---|---|---|
| ☐ 1.1 | Wiring continuity against the pin table (GPIO25, 26, 27, 34, 35, 36, 39, 32, 33, 4) | |
| ☐ 1.2 | **EN pull-down**: 8 to 11 kΩ from R_EN/L_EN to GND at the IBT-2, and at GPIO27 | kΩ |
| ☐ 1.3 | Optional 10 kΩ pull-downs on GPIO25, 26 (if fitted) | |
| ☐ 1.4 | Dividers: 100k/22k supply, 10k/20k current sense and Hall; Hall at 5 V, output through its divider | |
| ☐ 1.5 | Park switches close when parked; rain contact closes when it rains | |
| ☐ 1.6 | Motor supply switch within reach; motor leads off the IBT-2 and insulated | |

## Stage 2: First power-up (motor still disconnected)

| | Check | Result |
|---|---|---|
| ☐ 2.1 | Flashed from USB, 12 V off: GPIO27 reads 0 V throughout; on WiFi, address noted | |
| ☐ 2.2 | GPIO25, 26, 27 read 0 V before, during and after start-up, and while held in reset | |
| ☐ 2.3 | Password protection on | |
| ☐ 2.4 | Boot between the magnets: `unknown`, EN low. At a magnet after a restart: that end | |
| ☐ 2.5 | `supplyVolts` within 0.2 V of the meter | V / V |
| ☐ 2.6 | Park switches and rain contact read `closed`/`open` and `raining`/`dry` over the real cable (external pull-up if not) | |
| ☐ 2.7 | Current sense left off until stage 8 | |

## Stage 3: Hall calibration (motor disconnected)

| | Check | Result |
|---|---|---|
| ☐ 3.1 | Counts at rest / closed end / open end | / / |
| ☐ 3.2 | Closest magnet approach stays clear of 30 and 4065 | |
| ☐ 3.3 | `roof.hallHighEnd`, `hallHighCounts`, `hallLowCounts` set | / / |
| ☐ 3.4 | States correct at each end, `unknown` between, also after a restart | |
| ☐ 3.5 | Sensor unplugged: `hallRail` fault latched; cleared after replugging | |

## Stage 4: Weather and park sources (motor disconnected)

| | Check | Result |
|---|---|---|
| ☐ 4.1 | SQMeter HTTP: `Weather safe` within one poll of boot | |
| ☐ 4.2 | SQMeter unsafe: `Weather unsafe` within 5 s; SQMeter unplugged: unsafe within 35 s | |
| ☐ 4.3 | Wrong URL path: unreadable, no reboot | |
| ☐ 4.4 | SQMeter MQTT (if used): offline at once; a retained `safe=1` alone isn't safe | |
| ☐ 4.5 | Rain contact (if fitted) makes it unsafe | |
| ☐ 4.6 | N.I.N.A. 2.2.15+ Advanced API: `Park: parked`; unparked and slewing correct; closing N.I.N.A. gives `not responding` in about 4 s | |
| ☐ 4.7 | Park switches vs N.I.N.A.: one switch open gives "disagree" | |
| ☐ 4.8 | An unrelated settings save doesn't reset the verdict or the park result | |
| ☐ 4.9 | **Automatic close** settings left at their defaults (10 min, 30 min, Halt, Remote Stop off, Advanced untouched) until stage 11 | |
| ☐ 4.9 | Site set (lat, lon); NTP synced; the Roof card's Daylight sun altitude within 0.5° of an almanac | ° vs ° |
| ☐ 4.10 | Daylight mode left on Alert or Off until stage 11 | |

## Stage 5: The alert channel

| | Check | Result |
|---|---|---|
| ☐ 5.1 | At least one channel set up | channel: |
| ☐ 5.2 | **Test** on each of the ten roof rows (the three daylight rows included) arrives on the phone with example values; wording and layout reviewed | |
| ☐ 5.3 | The phone's silent mode lets Wake me through | |
| ☐ 5.4 | With **Send alerts** off, a critical **Test** still arrives | |
| ☐ 5.5 | MQTT only: the critical alert over MQTT carries the reason | |
| ☐ 5.6 | Real roof events arrive in stages 6 and 7 (ticked there) | |

## Stage 6: The driver and the faults (motor disconnected, hand-held magnet)

| | Check | Result |
|---|---|---|
| ☐ 6.1 | Open refused with no weather source; close refused with N.I.N.A. off or a park switch open; a switch opened mid-close stops with `parkInterlock`; between the magnets only a person's open or close is accepted (not N.I.N.A. or MQTT) | |
| ☐ 6.2 | EN high only while driving; RPWM and LPWM never both high; 20 kHz | |
| ☐ 6.3 | Stop drops EN at once; `stopped`; a start waits 0.5 s | |
| ☐ 6.4 | No magnet: `timeout` fault after the expected time plus margin | s |
| ☐ 6.5 | Magnet held at the end: `didNotLeave` after the leave time | s |
| ☐ 6.6 | Fault stays latched across 5 power cycles; only `roofclear` clears it; a restart right after a fault keeps it | |
| ☐ 6.7 | During a move: settings save, upload and restart answer `409` | |
| ☐ 6.8 | Hall unplugged mid-move: `hallRail`, **Roof fault** alert arrives | |
| ☐ 6.9 | `roof.minSupplyVolts` 15: refused, **12 V supply low** after about 30 s; value restored; sense wire unplugged reads 0 and open and close are refused | |
| ☐ 6.10 | SQMeter off past stale time plus 30 s: **Weather source lost** arrives | |
| ☐ 6.11 | Power pulled mid-move: reset reason logged, `unknown`, no motion | |
| ☐ 6.12 | Reset with a fault latched: no repeat alert; after a power cut one alert after 60 s | |
| ☐ 6.13 | Dashboard follows: Roof card state and Hall marker; Close and Open grey with reasons; fault on the Roof and Status cards and Clear fault; "No live data" greys but Stop sends; Stop bar on other pages; looked at on a phone in the dark | |

## Stage 7: Close-on-unsafe dry run (motor disconnected)

`park.timeoutSeconds` set to 30 for the test, then to the real value: __ s

| | Check | Result |
|---|---|---|
| ☐ 7.1 | Unsafe with mount unparked: park request in 1 s; close 4 to 5 s after parked; **Unsafe: closing** and **Roof closed after unsafe weather** on the phone | |
| ☐ 7.2 | N.I.N.A. closed: **Mount didn't park** at the timeout, no request for 5 min, roof stays open; also with **Send alerts** off | |
| ☐ 7.3 | Stop during the close: stops, **Automatic close stopped**, nothing more; stop in the 4 s before the close: no close | |
| ☐ 7.4 | After a stop, restart: still stood down, **Still:** alert | |
| ☐ 7.5 | Between the magnets, unsafe, restart: no park request, **Roof can't close by itself** after 2 s | |
| ☐ 7.6 | Flapping verdict: requests at most every 30 s, **Mount didn't park** still arrives | |
| ☐ 7.7 | Roof alerts arrive with the master switch off and one channel on | |
| ☐ 7.8 | During the wait for park: restart, firmware upload, web UI upload, WiFi change answer `409`; after Stop they are accepted; with the router off they work from the hotspot | |
| ☐ 7.9 | Hotspot on: open answers `409 hotspot`; close and stop work | |
| ☐ 7.10 | WiFi change with the roof open: a close waits, then starts (15 s at most); MQTT save with broker down answers in 1 s | |
| ☐ 7.11 | Close during a web upload stops it (`409`) and closes; pulled cable doesn't block a close more than 5 s | |
| ☐ 7.12 | Command-line OTA (if used): restarts through the plan; open during upload refused | |
| ☐ 7.13 | Daylight dry run (by day): Alert sends **Roof open in daylight**, nothing moves; Alert and close parks, closes, **Roof closed after daylight**; open refused `daylight`; N.I.N.A. closed gives **Mount didn't park** "It is daylight"; Stop stands it down with one alert and no reminders, unsafe weather 30 s later starts the weather close; no site: **Daylight check not working** after 10 min; mode set back | |
| ☐ 7.14 | N.I.N.A. over Alpaca with the motor disconnected: steps 9.1 to 9.7 below | |

Between steps: park, close, present the closed magnet, so the stand-down and the alert limit are cleared. **Send alerts** back on: ☐

**Second gate:** ☐ Stage 7 passed, with its alerts on the phone and the close sequence idle. Now switch off, connect the motor, park the mount, second person at the supply switch.

## Stage 8: First attended moves (motor connected)

| | Check | Result |
|---|---|---|
| ☐ 8.1 | `roof.topDutyPercent` 40 and `roof.leaveMagnetSeconds` 3 for the first moves | |
| ☐ 8.2 | First move leaves the closed end the right way; `roof.openDirection` | |
| ☐ 8.3 | Stop at once; carries on from `stopped` | |
| ☐ 8.4 | Full open and close at 40%: ends at the magnet within the timeout, not hard on the stops | s / s |
| ☐ 8.5 | Travel (tape) and speed set from the 40% time before any faster move, refined at 70% then 100%; 40% time about 2.5 times the 100% time, margin raised if not; leave time back to 10 | mm / s / mm/s |
| ☐ 8.6 | No `supplyLow` at start; lowest supply during a move | V |
| ☐ 8.7 | Timeout test mid-travel stops the roof, values restored; motor and IBT-2 cool enough after the longest run | |
| ☐ 8.8 | Current sense (if fitted): `currentAmpsPerVolt`, `maxCurrentAmps`, `minRunningAmps`; `overCurrent` test; surge under 250 ms | / / |
| ☐ 8.9 | Power cut mid-move: roof held, `unknown`, only a person moves it, slowly | |
| ☐ 8.10 | Full run at the settled values, attended | |
| ☐ 8.11 | **Attended dry run on the real roof**: park, close, **Unsafe: closing**, **Roof closed after unsafe weather**; a stop during the close works | |

**Third gate:** ☐ Stage 8 passed.

## Stage 9: N.I.N.A. over Alpaca and ConformU

| | Check | Result |
|---|---|---|
| ☐ 9.1 | (steps 9.1 to 9.7 first with the motor disconnected, see 7.14; 9.2 and 9.5 again on the real roof) Alpaca on, restarted; N.I.N.A. discovers **RoofRoof roof** and connects | |
| ☐ 9.2 | Open and close from N.I.N.A.; ShutterStatus runs opening, open, closing, closed | |
| ☐ 9.3 | Unsafe: Open shows the weather reason, nothing moves | |
| ☐ 9.4 | Unparked: Close shows the park reason | |
| ☐ 9.5 | N.I.N.A. Stop works in safe weather; with Remote Stop off, in an unsafe close it shows an error and the close continues; web UI Stop still stops it; no **Automatic close stopped** alert | |
| ☐ 9.6 | After a boot between the magnets, Open and Close say only a person can move the roof | |
| ☐ 9.7 | Alpaca off: every Dome command, Stop included, says "disabled" | |
| ☐ 9.8 | ConformU (bench-build board with GPIO36 to GND, or the roof's board with the motor disconnected or attended) passes with no errors | |

## Stage 10: Home Assistant and MQTT (optional)

| | Check | Result |
|---|---|---|
| ☐ 10.1 | Roof cover, position, supply, Hall and fault entities appear (motor current only with current sense); they follow a move; a fault turns the fault sensor on | |
| ☐ 10.2 | Commands off: `OPEN` answers `switchedOff`; toggle wording reviewed | |
| ☐ 10.3 | Commands on (optional): OPEN and STOP work; a retained `OPEN` answers `retained`; unknown position answers `positionUnknown`; STOP in an unsafe close answers `unsafeClose` | |
| ☐ 10.4 | Broker cable pulled during a close: the close completes, no `parkInterlock` | |
| ☐ 10.5 | Discovery burst on the real WiFi: no write-budget disconnect | |

## Stage 11: The gate for unattended use

| | Check | Result |
|---|---|---|
| ☐ 11.1 | An alert reached the phone (stage 5), and the critical events in the dry runs (stages 7 and 8); critical alerts arrived with **Send alerts** off, which is now back on | |
| ☐ 11.2 | The dry run passed with the motor disconnected (stage 7) and attended on the real roof (8.11); `park.timeoutSeconds` a little above the mount's longest park | s |
| ☐ 11.3 | Password protection on; settings backed up; values recorded below | |
| ☐ 11.4 | After an hour or more: `maxWatchdogBeatAgeMs` under 50, `maxStepGapMs` near 20, `watchdogTrips` 0, `stackFree.roof` above 2048 | / / / |
| ☐ 11.5 | Daylight mode chosen (Alert and close, Alert or Off), site set, daylight dry run passed | mode: |
| ☐ 11.6 | Automatic close settings chosen; any changed one re-tested in a dry run; with Remote Stop on, N.I.N.A.'s Stop in the attended unsafe close gave **Automatic close stopped** | |
| ☐ 11.7 | First unattended spell short, phone alerts on | |

## Values to record

| Setting | Value |
|---|---|
| `roof.travelMm` | |
| `roof.speedMmPerSecond` | |
| `roof.openDirection` | |
| `roof.hallHighEnd` / `hallHighCounts` / `hallLowCounts` | |
| `roof.timeoutMarginSeconds` / `leaveMagnetSeconds` | |
| `roof.topDutyPercent` / `slowDutyPercent` | |
| `roof.currentSenseEnabled` / `maxCurrentAmps` / `currentAmpsPerVolt` / `minRunningAmps` | |
| `roof.minSupplyVolts` | |
| `park.switches` / `park.timeoutSeconds` | |
| `safety.staleSeconds` | |
| `daylight.mode` / `daylight.sunAltitudeDeg` / `daylight.bandDeg` | |
| `autoClose.rearmMinutes` / `remindMinutes` / `notStarted` / `remoteStop` | |
| `autoClose.parkRequestSeconds` / `parkHoldSeconds` / `retryMinutes` / `closeStartSeconds` / `closeGuardSeconds` | |
| `location.latitude` / `location.longitude` | |

Signed off by ______________________ on ______________
