---
description: "What RoofRoof does when the weather turns unsafe, or the sun is up and the daylight failsafe is set to close: ask N.I.N.A. to park, wait for the confirmed park, close. Its time limits, what each critical alert means, and what to do."
---

# Close on unsafe weather

When the weather is not safe and the roof is not closed, RoofRoof closes it by itself:

1. it asks N.I.N.A. to park the mount;
2. it waits until the park is **confirmed**: the [park rule](safety-inputs.md#park) (N.I.N.A. says
   parked AND every fitted park switch is closed) has said parked for 4 s without a break;
3. it closes the roof through the same checks as any other close (the park is checked at the
   start and on every control step while closing), and watches it reach the closed magnet.

The roof **never opens by itself**, and you can't open it while the weather isn't safe: there is
no override. An open already under way stops as soon as the weather isn't safe, so the roof opens
no further; the sequence then parks and closes from there.

"Not safe" means unsafe *or unknown*: doubt counts as unsafe. After a restart with the roof open,
the device asks for a park until a weather source has said safe.

Every step has a time limit. A step that runs out stops the sequence where it is and sends a
critical alert naming the step. The roof is never closed onto a mount that isn't confirmed parked:
a wet telescope can be dried, a crushed one can't be fixed.

<!-- diagram: DIA-22
sources: lib/CloseSequenceLogic/src/CloseSequence.cpp lib/CloseSequenceLogic/src/CloseSequenceDaylight.cpp lib/CloseSequenceLogic/include/CloseSequence.h
blocking: true
fingerprint: b089d4bcff8a5aee
-->
<figure class="diagram" markdown>

```mermaid
stateDiagram-v2
    accTitle: The close on unsafe weather
    accDescr: From idle, an unsafe verdict with the roof not closed starts an attempt that asks N.I.N.A. to park; a roof already closing is only watched. When the park has held for 4 s the roof is closed. Reaching the closed magnet ends the sequence. A park not confirmed in time, a refused, interrupted or too-long close leads to a five-minute wait, during which a confirmed park still closes the roof, then a new attempt. A fault, or a position unknown for 2 s, stands the sequence down until a person acts. A close that outlasts its guard is stopped and the sequence halts. A person's stop (also while it can't move) stands it down; halted and stood-down sequences end when the roof is closed or it has been safe for 10 minutes. A safe verdict before the close ends the sequence; a close already commanded runs to the end. With the daylight close on, "not safe" also means the sun is up and "safe" means neither; when the weather turns unsafe after a daylight-only attempt, a waiting or can't-move sequence starts again at once, a halted one or one a person stood down after 30 s of unsafe weather in all.
    [*] --> Idle
    Idle --> WaitPark: not safe, roof not closed (park request)
    Idle --> Closing: not safe, a close already running
    Idle --> CannotMove: not safe, fault, or position unknown for 2 s
    WaitPark --> Closing: park held for 4 s (close)
    WaitPark --> RetryWait: park not confirmed in time (critical alert)
    WaitPark --> Idle: safe
    RetryWait --> Closing: park held for 4 s (close)
    RetryWait --> WaitPark: after 5 min (park request)
    RetryWait --> Idle: safe
    Closing --> Idle: closed magnet read
    Closing --> RetryWait: close refused or interrupted (critical alert)
    Closing --> WaitPark: close not run (park no longer held, or the device about to restart)
    Closing --> Halted: too long (stop, critical alert)
    Closing --> CannotMove: fault (critical alert)
    WaitPark --> HeldByPerson: a person's stop (critical alert)
    Closing --> HeldByPerson: a person's stop (critical alert)
    RetryWait --> HeldByPerson: a person's stop (critical alert)
    CannotMove --> RetryWait: a person made the position known or cleared the fault
    CannotMove --> HeldByPerson: a person's stop (critical alert)
    CannotMove --> Idle: closed, or safe for 10 min
    HeldByPerson --> Idle: closed, or safe for 10 min
    Halted --> Idle: closed, or safe for 10 min
    HeldByPerson --> Idle: weather unsafe 30 s in all, daylight-only attempt (starts again)
    Halted --> Idle: weather unsafe 30 s in all, daylight-only attempt (starts again)
    RetryWait --> Idle: weather unsafe, daylight-only attempt (starts again)
    CannotMove --> Idle: weather unsafe, daylight-only attempt (starts again)
```

<figcaption>The close on unsafe weather (and, with the daylight close on, in daylight: "not safe" then also means the sun is up). From waiting or closing, a fault or a position unknown for 2 s also stands the sequence down (CannotMove), and the roof reading closed always ends it.</figcaption>
</figure>

??? info "Diagram in words"

    1. While the weather is safe, or the roof is closed, nothing happens (`Idle`).
    2. When the weather is not safe and the roof is open, stopped part-way or opening, an attempt starts: N.I.N.A. is asked to park (`WaitPark`). If a close is already running (a person's, or one waiting for an upload to stop), it is only watched (`Closing`).
    3. When the park result has said parked for 4 s without a break, the roof is closed (`Closing`). Reaching the closed magnet ends the sequence.
    4. If the park isn't confirmed within `park.timeoutSeconds` (at once if N.I.N.A. isn't set up), a critical alert says the mount didn't park and the roof stays open (`RetryWait`). For 5 minutes no park request is sent, but a confirmed park still closes the roof. Then a new attempt starts.
    5. If the close is refused or stops part-way, a critical alert says the roof didn't close, then the same 5-minute wait; no close is commanded in those 5 minutes, even if the weather clears and turns again. A close the roof doesn't run because the park stopped holding at that moment, or because the device is about to restart, isn't a failure: the sequence goes back to the step it came from, and the park must hold for 4 s again.
    6. If the close takes longer than its own timeout plus 30 s (or a waiting close doesn't start within 30 s), it is stopped where it is, a critical alert says so, and the sequence halts (`Halted`): nothing more is sent or moved.
    7. If the roof faults, or its position is unknown for 2 s, nothing is sent and a critical alert says it can't close by itself (`CannotMove`). Once a person has made the position known or cleared the fault, the 5-minute wait follows.
    8. A person's stop or abort during the sequence, or while it can't move, stands it down (`HeldByPerson`) with a critical alert: nothing more is sent or moved.
    9. `CannotMove`, `HeldByPerson` and `Halted` end when the roof reads closed, or the weather has been safe for 10 minutes without a break.
    10. A safe verdict while waiting ends the sequence (with the daylight close on, only when the sun isn't up either). A close already commanded runs to the closed magnet. If it turns unsafe again within 10 minutes, the attempt carries on where it was, its time limit still running.
    11. An attempt that began only because of daylight is a daylight attempt until the weather first turns unsafe. Then a waiting or stood-down (can't move) daylight attempt ends at once (`Idle`) and the weather attempt starts on the same pass; one a person stood down, or a halted one, ends once the weather has been not safe for 30 s in all since the stop, added up over unsafe spells (a new stop starts the count again); a person's stop on that very pass still stands it down.
12. A restart of the device (not a power cut) keeps a stand-down, and an attempt or back-off with the time it has run (plus 10 s for the restart), so a board that keeps restarting still reaches its limits. A restart after the close was asked for but before the roof took it carries on from the park wait; it isn't a failed close.

## Time limits

| What | Limit | Then |
|---|---|---|
| The park confirmed | `park.timeoutSeconds`, default 240 s (30-1800); immediately without N.I.N.A. | "Mount didn't park" (critical); the roof stays open |
| Between park requests | at least 30 s, whatever the weather does; repeated every 30 s while waiting | - |
| The park must hold before closing | 4 s without a break | - |
| After a failed attempt | 5 minutes before the next park request | a new attempt |
| A close waiting for an upload or MQTT connect | 30 s to start | stopped; "Roof didn't close" (critical); halted |
| A close in motion | its own timeout + 30 s from the command (the motion logic's own timeout normally stops it first) | stopped where it is; "Roof didn't close" (critical); halted |
| After a refused or interrupted close | 5 minutes with no close | - |
| Position unknown | 2 s (the moments after a restart don't count) | "Roof can't close by itself" (critical) |
| Standing down or halted ends | the roof closed, or safe for 10 minutes without a break since standing down | back to idle |
| A safe spell during an attempt | shorter than 10 minutes: the attempt carries on, its clock still running | - |
| Alerts | each kind at most every 30 minutes; repeated every 30 minutes while the roof stays open after one | - |

Set `park.timeoutSeconds` (**Settings → Roof → Mount park → Park time limit**) to a little more
than your mount takes to park from its furthest position; see
[Configuration](configuration.md#safety-and-park).

## Alerts

The four critical cases below are two alert events: **The mount doesn't park**
(`mount_not_parked`) and **The roof doesn't close** (`roof_not_closed`, the other three). In
**Settings → Alerts → Roof safety** you choose their level (**Urgent** or **Wake me**, default Wake
me: Pushover emergency, ntfy `max`), sound and wording; they can't be switched off. They go to every
alert channel that is switched on, even while alerts are paused or switched off as a whole: only the
channels you have switched on are used, so set at least one up. An alert no channel took (WiFi down
just after a start, a failed send) is sent again after a minute, then at doubling gaps up to 30
minutes, until one does. Each kind is sent at most every 30 minutes; while the roof stays open after
one, it is sent again every 30 minutes with "Still:" in front of the title. None of them goes through
N.I.N.A. or SQMeter, so losing either doesn't lose the alert.

The start of the sequence (**The weather closes the roof**) and the roof reaching closed (**Roof
closed after unsafe weather**) are ordinary alert events with the usual rules (the master switch, the
pause, the cooldown); see [Alerts](alerts.md#the-roof).

| Alert | Means | What to do |
|---|---|---|
| **Mount didn't park** | The park wasn't confirmed in time; the message gives the park result's reasons (`N.I.N.A. unreachable`, `park switch 1 open`, `sources disagree`...). The roof is open. | Park the mount (N.I.N.A., or by hand). Once the park is confirmed, the roof closes by itself. Or close it yourself once parked. |
| **Roof didn't close** | The close faulted, was refused (with the reason), stopped part-way, or took too long (then it stays stopped and isn't tried again until the roof has been closed or the weather has been safe for 10 minutes; after that, a later unsafe spell starts a new automatic close). | Look at the roof. After a fault, fix the cause and clear the fault; the close resumes once the mount is confirmed parked. After "took too long", close it yourself once you've checked it. |
| **Roof can't close by itself** | A fault is latched, the roof's position is unknown (after a restart between the magnets, or an abort), or the roof controller didn't start. Nothing is sent or moved. | Move the roof to a magnet, or clear the fault. If it is still unsafe, the roof closes once the mount is confirmed parked. |
| **Automatic close stopped** | A person stopped or aborted the roof during the sequence (or while it couldn't move). Nothing more happens while it is unsafe. | Close the roof yourself when it is safe to, or wait for 10 minutes of safe weather. |

The titles above are the built-in wording. Your own wording (with `{reason}`, `{limit}` and
`{details}`) keeps the reason: a message that uses neither `{reason}` nor `{details}` gets the
built-in message added under it.

!!! warning "A stop is respected"
    The automatic close never overrides a person's stop: whoever pressed it may be at the roof.
    A person's stop is the web UI's or the REST API's (logged in). An imaging app's Stop over
    [Alpaca](alpaca.md), or a STOP over [MQTT](mqtt.md#roof-commands) (Home Assistant), can't stop a
    close while the weather isn't safe: neither has a password, so it is refused, and it never
    counts as a person's stop.
    The stand-down, and an attempt's running time, survive a restart of the device (a crash, a
    reset, a brown-out), so a board that keeps restarting in the rain still alerts in time; not a
    power cut, and not
    reliably a firmware update.
    A stop pressed by mistake leaves the roof open in the rain until someone acts on the alert.

## Daylight failsafe

The same sequence also runs when the sun is up and the roof isn't closed, if you ask for it. Set
**Settings → Roof → Daylight** to one of three modes (`daylight.mode`):

| Mode | When the sun is up and the roof isn't closed |
|---|---|
| **Off** | Nothing. Solar imaging works as it does without the feature. |
| **Alert** (the default) | An alert says the roof is open in daylight. Nothing moves, and nothing is refused. |
| **Alert and close** | The roof is parked and closed as for unsafe weather. Opening is refused while the sun is up. |

**Alert** is the default, also after an update from a release without it. Some owners image the sun
on purpose, for them an open roof in daylight is normal, and an update never turns on a feature that
moves the roof. Alert tells every owner about a forgotten open roof and moves nothing. Pick **Alert
and close** once if you never image the sun.

**The sun is up** when its altitude is at or above the **Sun altitude** threshold
(`daylight.sunAltitudeDeg`, default 0 degrees, the sun's centre on the horizon; -18 to +30). A lower
value closes earlier, a higher one suits a site with a high horizon. The sun counts as down again
only once it is 0.5 degrees below the threshold, so a sun that lingers there doesn't flicker the
state. The altitude is worked out on the device, once a second, from the site and the clock.

**The site and the clock.** Enter the latitude and longitude in **Settings → Roof → Daylight**
(`location.set`, `location.latitude`, `location.longitude`; RoofRoof has no GPS). Without the site, or
before the clock is set by NTP, the check can't tell day from night, so it **does nothing**: no
alert, no close, no refusal. With the mode on (alert, or alert and close), it sends **Daylight check
not working** (naming what is missing) once it has been unable to run for 10 minutes. A clock set
within 10 minutes of a restart never alerts. With the mode off, nothing is sent.

**Alert and close** is spec 005's sequence, unchanged: it asks N.I.N.A. to park, closes only once the
park is confirmed, never onto an unparked mount, and uses the same time limits, back-off and critical
alerts, worded for daylight (see below). Nothing in it opens the roof.

**Opening.** While the sun is up in **Alert and close**, every open is refused (web UI, REST,
[Alpaca](alpaca.md), [MQTT](mqtt.md) and Home Assistant) with the reason `daylight` and the sun's
altitude and the threshold (`Daylight: Sun 3.2° ≥ 0.0°`). An open under way is stopped, as it is for
unsafe weather; the sequence then parks and closes. When the weather is also unsafe, the weather
reason is the one named. **Off** and **Alert** never refuse or stop anything. To open the roof in
daylight, set the mode to Alert or Off first.

**A person's Stop.** A person's Stop (web UI, REST) stands a daylight close down, exactly as it
does a weather close. An Alpaca or MQTT client's stop of a close in daylight is refused
(`unsafeClose`), as in bad weather. The stand-down lasts while the sun is up. It ends once the sun
has been down, or the mode has not been **Alert and close**, for 10 minutes, or when the roof reads
closed. A halted close stays halted the same way. Daylight never ends a stand-down or a halt.
**Automatic close stopped** goes out once when you stop a daylight close, with no 30-minute
"Still:" reminders while the weather is safe: you stopped it on purpose. Set the mode to **Alert** if you want the roof open
for the rest of the day.

**Unsafe weather comes first** and is unchanged:

- If the weather turns unsafe while a daylight-only attempt is backing off after a failure or
  unable to move, the weather close starts at once (a roof that can't move raises **Roof can't
  close by itself** again, worded for the weather), as a fresh attempt: the daylight close's
  back-off doesn't hold it back, also when daylight ended just before. If a person stood the
  daylight close down, or a daylight close was halted, the weather
  close starts once the weather has been not safe for 30 s in all since your stop, added up over
  the unsafe spells (a single missed SQMeter poll doesn't send a park request against your stop,
  and a flapping verdict still gets there; a new Stop starts the count again; the count is kept
  across a restart). While the weather
  is unsafe, the stand-down's 30-minute reminder comes back. A person can
  stop the weather close with Stop too. The critical alerts start afresh, so a weather close that
  fails the same way as the daylight one just did is still announced. An
  attempt already waiting for the park, or a close under way, carries on, and its alerts are worded
  for the weather from then on.
- A weather close stood down by a person stays stood down while the sun is up in **Alert and
  close**, even after the weather clears. It re-arms once the sun has been down for 10 minutes. The
  roof doesn't move either way, and the weather close's reminders keep coming while the roof is
  open in daylight.
- With the mode **Off** or **Alert**, the sequence is exactly the one described above.

**Alerts.** Three events in [Settings → Alerts](alerts.md#the-roof): **Roof open in daylight**,
**Roof closed after daylight** and **Daylight check not working**. When a daylight close can't
finish, the same critical alerts as above go out, with "It is daylight" in place of "The weather is
unsafe" ("Mount didn't park: It is daylight and the park wasn't confirmed within 240 s, so the roof
stays open"), and the stood-down alert says the roof stays open "in daylight" and what re-arms it:
the sun has been down (or the daylight close off) for 10 minutes. **Roof open in daylight** isn't
sent while the weather close is already closing the roof ("The weather closes the roof" has said so).
The daylight events are ordinary alerts: they wait while alerts are paused, including **Only while
an imaging app is connected** (a crashed imaging PC disconnects, so that setting also silences
**Roof open in daylight**; use **Any time** if you rely on it). After an update, a device with no
site set sends **Daylight check not working: Location not set** once, 10 minutes after the restart:
set the site, or set the mode to Off.

The Roof card's **Daylight** line shows the mode and the sun against the threshold, or why the check
can't run; see [the dashboard](dashboard.md#roof).

## In the log

Every step is logged with the `Close` tag: `Close sequence started: unsafe: ...`, `Asking N.I.N.A.
to park the mount`, `Closing the roof: the mount is parked`, each phase change, and every alert's
title and message.
