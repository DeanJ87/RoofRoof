<!--
Sync Impact Report
- Version: 12.0.0 -> 12.1.0 (MINOR: Principle II gains a rule for park requests. Nothing is
  relaxed: RoofRoof sends fewer park requests than before, never more.)
- Modified: II. Close Only Onto a Parked Mount. New: **a park request never stops a slew**. N.I.N.A.'s
  Advanced API stops a slewing mount before it parks it, and a mount already parking reads slewing,
  so RoofRoof doesn't ask N.I.N.A. to park while a reading taken after the request (or the last good
  one, within its freshness) says the mount is slewing; at park it sends nothing; otherwise it asks
  once. A slew that hasn't ended after 180 s gets the park anyway, logged. Requests while one waits
  join it. The roof-at-an-end rule (11.1.0) is checked again when it is sent. The park check is
  unchanged: only AtPark in a fresh reading (with the switches) confirms the park.
- Why: findings F57/F59 (spec 050), twice on the real OnStep mount: N.I.N.A.'s sequence parked the
  mount, RoofRoof's automatic close asked for the park a few seconds later, the API stopped the park
  part-way and the mount was left unparked; the roof (rightly) couldn't close.
- Templates: no change. Docs: docs/user-guide/close-on-unsafe.md, docs/user-guide/alpaca.md,
  specs/050-park-only-when-still/spec.md.
- Earlier: 11.1.0 -> 12.0.0 (MAJOR: Principle II relaxes its re-check during a move. A park reading
  past its freshness no longer stops a move that started with the park confirmed.)
- Modified: II. Close Only Onto a Parked Mount. The re-check bullet no longer says "evidence past its
  freshness stops the motor the same way". New: **a command is a command**. An open or a close (any
  origin) that started with the park confirmed by a fresh reading and every fitted switch closed
  goes on to its end when N.I.N.A. merely stops answering (no reply, the network down, an
  unreadable reply, an API error, its last reading past its freshness), on N.I.N.A.'s last answer
  having said parked. It still stops on a fresh answer that says "not parked" (not at park,
  slewing, mount not connected), the sources disagreeing, a switch, any fault, Stop, a stalled
  hand-over of the safety inputs, and for an open the weather and daylight. The start rule is
  unchanged. A move that finishes this way is logged and raises an information alert. The
  interlock's fault names the move's direction ("Park lost during the open").
- New risk (II): a mount unparked while N.I.N.A. can't be read (by a person, or N.I.N.A.'s own
  sequence) is seen only by the park switches; the roof can go on over it, and with N.I.N.A. as the
  only park source nothing sees it. Mitigations: a move only starts on a fresh confirmed park, so
  the window is one move's travel; any fresh answer from N.I.N.A. stops it at once; the switches
  are read every control step; the log and the alert say it happened; a stalled controller still
  stops it.
- Why: owner decision (spec 047, findings F36/F36b): three times on the real roof a close (and once
  an open) started with the park confirmed, the network dropped, the park reading went stale, and
  the interlock stopped the roof part-open, where neither the roof nor the telescope may move
  (11.1.0). "A command is a command: if the WiFi drops it shouldn't just stick."
- Modified: "Who May Set a Check Aside": the automatic exceptions name the move that goes on when
  N.I.N.A. stops answering (II).
- Templates: no change. Docs: docs/user-guide/close-on-unsafe.md, docs/user-guide/safety-inputs.md,
  docs/user-guide/alerts.md, docs/api/rest.md, specs/047-moves-finish-on-stale/spec.md.
- Earlier: 11.0.0 -> 11.1.0 (MINOR: Principle II gains a rule for the telescope, and Principle IV a
  second Alpaca device. Nothing is relaxed: the automatic close and a command's park wait ask for a
  park in fewer cases than before.)
- Modified: II. Close Only Onto a Parked Mount. New: **the telescope moves only with the roof at an
  end**. A slew, a park and an unpark are safe only with the roof at rest on its closed or open
  magnet; part-open, moving, with the position unknown or a fault latched, the telescope hits the
  roof's beams, which is worse than rain. RoofRoof never asks N.I.N.A. to park (a slew) unless the
  roof is at an end: neither the automatic close (also from an unknown position, and before a blind
  close) nor a command's park wait. With the roof not at an end and the mount not parked, nothing
  moves: no park request, no close (the roof can't close onto an unparked mount either), and the
  critical needs-a-person alert ("Roof part-open (N %) with the mount not parked: moving the roof or
  the telescope would hit the beams. A person must check.") is reminded until the roof is at an end,
  the mount is parked or nothing wants the roof closed. With the mount already parked those closes
  go ahead as before. The owner's N.I.N.A.-lost close (off by default) is unchanged: it asks for no
  park. A person may still move either through the existing confirmed paths, warned.
- Modified: IV. Standards Conformance. The device also offers a read-only Alpaca **Switch** (device 0,
  ISwitchV3): "Mount may move" (the roof at an end), "Roof fully open", "Roof fully closed", so an
  imaging sequence can gate its own slews. No client can write it. ConformU covers it as it covers
  the Dome.
- Why: finding F41 (spec 043): a roof stopped part-way (a person's Stop, a fault, a crash, a power
  cut) with the mount unparked, then unsafe weather: the automatic close asked N.I.N.A. to park, and
  the telescope slewed under the part-open roof.
- Templates: no change. Docs: docs/user-guide/alpaca.md, docs/user-guide/close-on-unsafe.md,
  docs/user-guide/dashboard.md, docs/user-guide/alerts.md, docs/api/rest.md,
  specs/043-slew-only-at-ends/spec.md.
- Earlier: 10.1.0 -> 11.0.0 (MAJOR: Principle III lets a person's Stop of the automatic close lapse by
  itself, as an owner setting, so the close can move the roof after a person stopped it.)
- Modified: III. Unsafe Ends Closed, "A person's stop". New: **the stop may lapse**
  (`autoClose.personStopLapse`, spec 044). `never` (the default, and after an upgrade) is the rule as
  it was: the stand-down never lapses while unsafe. `rain` lets it lapse after
  `autoClose.stopHoldMinutes` (5 min, 2-60) of rain or a failed rain sensor, added up since the stop;
  `always` after that long of any unsafe weather. A new Stop starts the count over. A lapse is logged
  and raises a critical alert ("Hold lapsed: closing because it's still raining"), and the close then
  goes through every step and gate (park request, park confirmed, close); a halted close and a
  daylight close's stop never lapse.
- Modified: "Who May Set a Check Aside": the automatic exceptions name the lapse of a person's stop
  (III, off by default).
- New risk (III): with the lapse set, the roof can start closing by itself minutes after a person
  stopped it, while that person may still be at the roof (a Stop pressed because the close would
  collide, not yet fixed). Mitigations: off by default and after an upgrade; only the owner sets it;
  at least 2 minutes; only while it is still raining (or unsafe, for `always`); a new Stop restarts
  the full count, and the dashboard, the status and Home Assistant count it down; the stop's own
  alert says when it lapses and the lapse raises a critical alert; the close still needs the park
  confirmed, so it never closes onto an unparked mount.
- Why: owner decision (spec 044): a Stop forgotten in the rain leaves the telescope wet until
  someone acts on the alert; an owner who worries about that more than a close they didn't expect
  may choose to let the stop lapse in rain, or in any unsafe weather.
- Templates: no change. Docs: docs/user-guide/close-on-unsafe.md, docs/user-guide/configuration.md,
  docs/api/rest.md, docs/integration/home-assistant.md, docs/user-guide/mqtt.md,
  specs/044-stop-hold-setting/spec.md.
- Earlier: 10.0.0 -> 10.1.0 (MINOR: Principle III is tightened. A failed rain sensor starts the
  automatic close at once, as rain does. Nothing is relaxed.)
- Modified: III. Unsafe Ends Closed, the weather hold. A failed rain sensor (SQMeter's rain-sensor
  fault, `reasonFlags` bit 10) skips `autoClose.unsafeHoldSeconds` like rain. It keeps its own
  reason ("rain sensor fault") in alerts and the UI. A missing, stale, unreachable, unreadable or
  offline source is still held, so a SQMeter that restarts doesn't close the roof. The hold's risk
  statement no longer covers a failed rain sensor.
- Why: owner decision (fix 042): a rain sensor that fails may be failing because it is wet, so
  waiting out the hold could leave the roof open in rain. A silent SQMeter keeps the hold: that is
  what stops its brown-outs closing the roof.
- Templates: no change. Docs: docs/user-guide/close-on-unsafe.md, docs/user-guide/safety-inputs.md,
  docs/user-guide/configuration.md, docs/user-guide/dashboard.md, docs/reference/troubleshooting.md,
  docs/hardware/commissioning.md, docs/hardware/commissioning-checklist.md,
  specs/030-weather-hold-and-blind-close/spec.md (marked superseded).
- Earlier: 9.1.0 -> 10.0.0 (MAJOR: Principle II lets a person open past the park, which relaxes it;
  a new section states who may set a safety check aside.)
- Added: "Who May Set a Check Aside" (before Principle I). The owner is the source of truth. A
  person, authenticated at the web UI, who can look at the roof or its camera, may be given a way
  past a safety check or fault, always with a warning that says what could happen and a deliberate
  confirmation, for that one move. An automation (an Alpaca client such as N.I.N.A., MQTT and Home
  Assistant, the controller's own automatic moves) never gets past a safety check, whatever it
  sends; the only automatic exceptions are the owner-decided ones Principles I-III name (the
  automatic close from an unknown position and its blind close, the close when N.I.N.A. is lost,
  the Hall rail fault clearing itself at rest, `autoClose.remoteStop`, the test box's bypasses),
  each off or bounded as it states. The checks a person can't yet get past are listed in spec 039
  as follow-ups; giving a person one more relaxes Principles I-III and takes a MAJOR amendment.
- Modified: II. Close Only Onto a Parked Mount. New: **a person's confirmed open** past the park,
  as for a confirmed close: warned that the roof passes over the telescope, a person may open once
  without the park check, at its start and during it (the park interlock doesn't stop that open;
  the weather, daylight, faults and Stop still do). It is logged and raises an information alert;
  the next open checks again. "No path skips the check" gains it. Alpaca, MQTT, Home Assistant and
  the automatic close never can.
- New risk (II): a person who confirms "Open anyway" with the mount not parked drives the roof
  over the telescope; if it isn't parked (the camera misread, or it moves during the open), the
  roof can hit it, and nothing stops that open for the park. Before, an open always needed the
  park confirmed and stopped when it was lost. Mitigations: only a person with the password, only
  from a dialog that says in red that the roof passes over the telescope and to check the camera,
  only for that one open, never from Alpaca, MQTT, Home Assistant or the automatic close (the
  override bits are filtered by origin where they are set and where they are kept); the device
  first asks N.I.N.A. to park when only the park blocks; a log line and an alert record it.
- Modified: III. Unsafe Ends Closed. A person's confirmed open now names the park among what may
  be overridden; rain and doubt about the weather source still can't be.
- Why: owner decision (spec 039): a person who can see the camera knows when the mount is parked
  and N.I.N.A. doesn't say so, and a roof that only N.I.N.A.'s word can open locks them out.
- Templates: no change. Docs: docs/user-guide/close-on-unsafe.md, docs/user-guide/dashboard.md,
  docs/user-guide/alerts.md, docs/api/rest.md, specs/039-person-overrides-park/spec.md.
- Earlier: 9.0.0 -> 9.1.0 (MINOR: Principle I is tightened. An end magnet counts only where the
  position estimate makes it plausible, a move that overruns its expected time without its magnet
  stops, and driving with no sign of movement stops. Nothing is relaxed.)
- Modified: I. Motion Safety. "Stop on its target magnet" now means a plausible reading: for a move
  from a known position the target's magnet is believed only once the estimate has covered
  `roof.endPlausiblePercent` of the travel; read earlier, the move goes on at the slow duty and
  accepts it only if it lasts and becomes plausible (or is clearly at the magnet) within a few
  seconds, else the end-too-early fault. That end then doesn't place the roof until the other end
  is reached or the Hall settings are saved again. New motion faults: no end magnet by
  `roof.endOverrunPercent` of the expected time; no movement (no supply sag of
  `roof.motionSagVolts` and no Hall change for `roof.motionCheckSeconds`). Each raises the
  needs-person alert at once.
- Why: field run (spec 036): a reading past a re-taught threshold a third of the way was taken as
  open, so Alpaca reported the shutter open; a shifted magnet let a move push into the end stop
  until the time guard; a low slow duty crawled for minutes with nothing seen.
- Templates: no change. Docs: docs/user-guide/roof-motion.md, specs/036-plausible-end-arrival/spec.md.
- Earlier: 8.0.0 -> 9.0.0 (MAJOR: Principle I lets a person nudge the roof with the Hall sensor
  failed, which no magnet can stop. It relaxes a safety principle, so it states its risk.)
- Modified: I. Motion Safety. A third owner-decided exception: a **person's blind jog**. With the
  Hall rail fault the latched fault (and no other), or no fault and the Hall reading at a rail or not
  settled, a person's jog (spec 019) runs on time alone: slow duty, only while the button is held
  (the keep-alive deadline on the step clock), at most the jog's limit (30 s at most), stopping on
  release, Stop, Abort, a stall seen as supply sag (`roof.blindSagVolts`) and every fault still
  checked. The Hall fault stays latched and the position unknown. Every other fault, invalid settings
  and a low or unreadable supply still refuse it. Alpaca, MQTT and the automatic origins can't. A
  person may also start one with the Hall sensor working, overriding the end magnets (a shifted
  magnet, a sensor reading wrong); afterwards a magnet read at rest doesn't place the roof until a
  move reaches one or new roof settings are saved.
- New risk (I): with no Hall sensor, or the magnets overridden, nothing stops the roof at its ends; the person watching, the
  keep-alive, the slow duty, the jog limit and the stall stop are the guards, and repeated nudges
  have no drive budget. The alternative, a part-open roof with a dead sensor that nobody can move
  from the web UI, was judged worse by the owner.
- Modified: II. Close Only Onto a Parked Mount, and III. Unsafe Ends Closed. A person's jog toward
  closed (normal or blind) is never held back by the weather, daylight, the park or the automatic
  close's state; with the park not confirmed the person is warned to watch the telescope. A jog
  toward open keeps every gate. New risk (II): with a wrong motor direction setting a "close" jog
  opens the roof over an unparked telescope until the person lets go.
- Why: owner decision after a roof was left part-open with a failed Hall sensor (spec 033).
- Templates: no change. Docs: docs/user-guide/roof-motion.md, docs/user-guide/calibration.md,
  specs/033-person-nudge-without-hall/spec.md.
- Earlier: 7.0.0 -> 8.0.0 (MAJOR: Principle I lets the automatic close move from an unknown position,
  with the Hall sensor working or failed; Principle III lets the automatic close wait out a hold
  before it starts for anything but rain; Principle II gains an owner-selectable close without
  N.I.N.A.'s park, off by default. Each relaxes a safety principle, so each states its risk.)
- Modified: I. Motion Safety. Two owner-decided exceptions to "never move by itself while its
  position is unknown", both for the automatic close only (unsafe weather, or daylight wanting the
  roof closed), only towards closed and only once the park is confirmed (Principle II, as for every
  close): (a) with the Hall sensor working, the close runs as a person's move from an unknown position
  does (slow duty, stops at the first magnet, the unknown-position timeout, every fault); (b) with the
  Hall sensor failed (a Hall rail fault latched and no other), and `roof.blindClose` on, a **blind
  close**: slow duty, towards closed on time alone, at most the unknown-position timeout, stopping on a
  stall seen as supply sag (`roof.blindSagVolts`), on any other fault and on Stop; once per latched
  fault, kept across restarts. Both end with a critical alert that a person must check the roof.
- New risk (I): (a) a roof whose position is unknown moves by itself; if the open-direction setting is
  wrong it drives open until the open magnet (a wrong-magnet fault) or the timeout. (b) with no Hall
  sensor nothing sees the closed end: the motor drives the chain into the end until the stall stop or
  the time limit (up to the full travel at the slow duty plus the margin), and a roof that was already
  closed is driven into its end. Mitigations: slow duty only; the hard time limit; the stall stop from
  the supply sag and the supply-minimum fault; one blind close per fault, remembered across restarts;
  the park rule; a critical alert every time; current sense is not fitted, so the sag is the only
  stall sign. The alternative, a roof that stays part-open in the rain with nobody told, was judged
  worse by the owner.
- Modified: II. Close Only Onto a Parked Mount. By default the roof never closes without the park
  confirmed by its park source, whatever the weather, rain included: the automatic close keeps asking
  and raises a critical alert, reminded until a person resolves it. New: **close when N.I.N.A. is
  lost** (`autoClose.ninaLostClose`, off by default and after an upgrade): with the park source N.I.N.A.
  or N.I.N.A. and switches, unsafe weather wanting the roof closed and N.I.N.A. unreadable without a
  break for `autoClose.ninaLostMinutes`, the automatic close (only) closes without N.I.N.A.'s park;
  with N.I.N.A. and switches only while every fitted switch reads closed. A critical alert follows.
- New risk (II): with the option on and N.I.N.A. as the only park source, the roof can close onto an
  unparked mount. Mitigations: off by default; only the automatic close, only for weather, only after
  minutes of N.I.N.A. silence; the switches still count when fitted; shown on its settings card.
- Modified: III. Unsafe Ends Closed. The verdict still turns unsafe at once (Open refused, an opening
  roof stopped), but the automatic close starts only once the unsafe time, added up and forgotten only
  after a safe spell as long, reaches `autoClose.unsafeHoldSeconds` (default 60 s, at most 300 s);
  rain starts it at once. Once started it isn't called off by a safe blip shorter than the hold.
- New risk (III): a real change of weather (cloud, wind, a failed rain sensor, a silent SQMeter) is
  closed up to the hold later than before: a silent SQMeter closes after its stale time plus the hold
  (about 95 s by default) instead of about 35 s. Mitigations: rain is never held; the hold is bounded;
  Open is refused at once; a SQMeter restart (a few seconds) no longer moves the roof.
- Modified: Compatibility. `roof.blindClose` is on after an upgrade (owner decision): the one
  motion feature a migration turns on, written here as the exception.
- Why: owner decisions after live testing on the real roof (spec 030): SQMeter restarts moved the
  roof; a crash mid-open left the roof part-open in unsafe weather with nothing moving.
- Templates: no change. Docs: docs/user-guide/close-on-unsafe.md, docs/user-guide/roof-motion.md,
  docs/user-guide/alerts.md, docs/user-guide/safety-inputs.md, docs/user-guide/configuration.md,
  specs/030-weather-hold-and-blind-close/spec.md.
- Earlier: 6.0.0 -> 7.0.0 (MAJOR: a person may confirm past two guards, which relaxes Principles II
  and III; a Hall rail reading at rest clears itself, which relaxes Principle I. The park check now
  also gates Open and commands wait for the park, which is more protective).
- Modified: I. Motion Safety. A Hall reading at a rail that latched with the motor off clears itself
  after 60 s of readings off the rails with the roof at rest, also after a restart (the stored fault
  record carries the flag; records from before 7.0.0 wait for a person); the position is then read
  afresh. One that latched while driving still waits for a person. A rail reading at rest in the
  first 500 ms after start-up isn't judged (a power-up transient latched the fault at every boot).
- New risk (I): an intermittent Hall connector at rest is no longer held for a person. Mitigations:
  the clear needs a minute of good readings at rest; a rail while driving still latches for a person
  and stops the motor at once; the position is unknown until the Hall reads a magnet again, so the
  roof can't move by itself from where it doesn't know it is.
- Modified: II. Close Only Onto a Parked Mount. The park rule gates Open as well as Close (a roll-off
  roof moving over a telescope that points up can hit it either way), and its re-check runs during an
  open. An Open or Close refused only for the park waits for it when N.I.N.A. can be asked (Principle
  III's park request, from every origin) and moves only once the park is confirmed, with the same time
  limit and critical alert as the automatic close. New: **a person's confirmed close**: a person,
  authenticated, after a warning that the mount is not confirmed parked, may close once without the
  park check (start and re-check). Alpaca, MQTT and the automatic close never can.
- New risk (II): a person who confirms "Close anyway" with the mount not parked closes onto it. Before,
  only a mount parked by N.I.N.A. or the switches could be closed onto. Mitigations: only a person
  with the password, only from a dialog that says the roof could hit the telescope, only for that one
  close (the next one checks again), never from Alpaca, MQTT, Home Assistant or the automatic close;
  the device first asks N.I.N.A. to park when it can.
- Modified: III. Unsafe Ends Closed. "Refused while the verdict is unsafe" gains **a person's confirmed
  open**: a person, authenticated, may open past daylight and past SQMeter's own "unsafe" for anything
  but rain, naming each reason. Rain, a failed rain sensor and every kind of doubt about the source
  (missing, stale, unreachable, unreadable, offline) can't be overridden. The override lasts until the
  roof reads closed, a Close, the overridden reasons clearing, or a reason not overridden appearing;
  meanwhile those reasons don't stop the open or start the automatic close.
- New risk (III): the roof can stand open in cloud or wind SQMeter calls unsafe, or in daylight, with
  the automatic close standing back for those reasons. Mitigations: never for rain or doubt about the
  source, which still close the roof at once; the override is shown on every interface while in
  effect; it ends by itself when its reasons clear; only a person with the password can set it.
- Clarified (III, no rule change): the test box's weather bypass stays out of effect while an SQMeter
  source is on, so a silent SQMeter still closes the roof.
- Why: owner decisions after the first nights with a real N.I.N.A. (spec 027).
- Templates: no change. Docs: docs/user-guide/close-on-unsafe.md, docs/user-guide/roof-motion.md,
  docs/user-guide/safety-inputs.md, docs/user-guide/dashboard.md, docs/user-guide/alpaca.md,
  specs/027-nina-park-and-open-guards/spec.md.
- Earlier: 5.0.1 -> 6.0.0 (MAJOR: Principle II's test-box exception loses a refusal, which relaxes
  it; Principle III states the stand-down after a person's stop and gains a way it ends, which is
  more protective).
- Modified: II. Close Only Onto a Parked Mount, the test-box exception. "No telescope" still never
  sits beside a N.I.N.A. address, but the save that turns it on now removes the address instead of
  being refused; an address can't be saved while it is on, and turning it off doesn't restore one.
- New risk (II): before, a configured N.I.N.A. address blocked the park bypass, so turning it on by
  mistake on a real install was refused. Now that one save (or any authenticated API client sending
  `testBox.park`) removes the address and bypasses the park check until the deadline: the roof
  could then close onto an unparked mount (fitted park switches still count). Mitigations: the test
  box page and the wizard say "This also removes the N.I.N.A. address" before the save; the bypass
  is shown on every page and interface; it ends at its deadline; the save needs the clock and an
  authenticated save, as before; the park source "switches" still can't use it.
- Modified: III. Unsafe Ends Closed. A new bullet states the stand-down that specs 005 and 026
  already implement (a person's stop holds the automatic close back; it ends when the roof reads
  closed, after the re-arm spell of safe weather, or by an attended release while nothing wants
  the roof closed) and adds that an Open by a person or a client that starts the roof moving ends
  it at once. Each way only arms the close again; none moves the roof. A close that took too long
  (halted) is not ended by an Open.
- Why: owner decisions after the first nights on the bench and the roof (spec 026's open
  questions). A person who stopped a close and then opened the roof again was left without the
  automatic close for the rest of the re-arm spell; a bench box could keep the address of a live
  N.I.N.A. and park a real mount.
- Not a constitution change, recorded here for the next reader: while the test box's park or
  weather bypass is in effect (or would be once the clock is set), the two critical roof alerts go
  out at urgent, not wake me (spec 022, spec 007). They are still critical: past the pause, the
  send mode and the master switch, and sent again until a channel takes them.
- Templates: no change. Docs: docs/user-guide/close-on-unsafe.md (DIA-22),
  docs/user-guide/test-box.md, docs/user-guide/alerts.md, specs/005-close-on-unsafe/spec.md,
  specs/022-test-box-bypass/spec.md, specs/007-roof-alerts/spec.md,
  specs/026-cohesive-journey/spec.md.
- Earlier: 5.0.0 -> 5.0.1 (PATCH: clarification). Principle II's test-box exception now says what
  "visible" rests on: the banner on every page and the status on every interface (API, MQTT, Home
  Assistant, the Alpaca description). The alert sent when a bypass comes into effect is an ordinary
  alert: the pause, the send mode and the master switch hold it, and a restart alone doesn't
  repeat it. This replaces the 3.0.0 note's "after every restart" mitigation; no rule changes.
- Why: an owner who paused alerts while flashing firmware still got the test-box alert after each
  restart.
- Templates: no change. Docs: docs/user-guide/test-box.md, docs/user-guide/alerts.md,
  specs/022-test-box-bypass/spec.md.
- Earlier: 4.1.1 -> 5.0.0 (MAJOR: Principle II no longer always requires N.I.N.A.; the owner may
  make the fitted park switches the only park source).
- Modified: II. Close Only Onto a Parked Mount. The park source is an owner setting
  (`park.source`): N.I.N.A. only, park switches only, or N.I.N.A. and park switches. Settings from
  before it take it from `park.switches` (none: N.I.N.A. only; one or two: N.I.N.A. and switches),
  exactly the rule before. With the switches only, every fitted switch reading closed confirms the
  park; with two, both must; N.I.N.A. counts only against it (a fresh reading that says the mount
  is slewing means not parked). The switches close to ground at park (normally open), so a broken
  wire reads not parked. A switch that hasn't read open over 3 roof openings in a row refuses Open
  until it has been seen open. With no N.I.N.A. reading relied on, the close's re-check reads the
  switches every control step and nothing rides through. The test box's park bypass (it stands in
  for N.I.N.A.) can't be set with the switches only. Principle III's step 2 (ask N.I.N.A. to park)
  is sent only when an address is set.
- New risk (II): with the switches only, a switch that is shorted to ground or stuck closed reads
  "parked" whatever the mount does. Two switches on separate cables make one such fault a
  disagreement (not parked); with one switch nothing can tell it from a parked mount. A fault that
  appears during a night, with one switch, can let the roof close onto an unparked mount; a fault
  that lasts is caught at the fourth roof opening at most (Open refused while the roof is closed).
  Settings, the setup wizard and the docs recommend two switches and warn with one.
- Why: spec 024. An observatory with reliable park switches, or without N.I.N.A. running at night,
  could not close the roof on the switches alone.
- Templates: no change (they read the principles from this file).
- Docs: docs/user-guide/safety-inputs.md, docs/hardware/wiring.md, docs/hardware/commissioning.md,
  specs/024-park-source-and-secrets/spec.md.
- Earlier: 4.1.0 -> 4.1.1 (PATCH: wording). Principle III's test-box exception and the 3.0.0 note
  below no longer name the rain contact that 4.0.0 removed: the bypass never overrides a reading
  (a configured source that says unsafe or rain). No rule changes.
- Earlier: 4.0.0 -> 4.1.0 (MINOR: Principle VII is expanded; nothing is relaxed).
- Modified: VII. Quiet, Consistent UI. The design system gains a second documented addition: the
  banner (`Banner` in `web/src/components/ui.tsx`, DS-14), for a state that holds on every page
  (bench firmware, test-box bypasses), built only from the tokens. A bypassed safety check is
  never shown as OK or parked: it reads as bypassed wherever the check is listed.
- Why: the test box banner (spec 022) was a bare line of text outside the page's column; the
  calibration checklists showed "Mount parked: OK" while the park check was bypassed.
- Templates: no change (they read the principles from this file).
- Docs: docs/development/coding-standards.md (DS-14, DS-01), docs/user-guide/test-box.md.
- Earlier: 3.0.0 -> 4.0.0 (MAJOR: Principle III loses a weather source; Principle I's pin and boot
  wording changes with the pins becoming settings).
- Modified: I. Motion Safety. EN, RPWM and LPWM are the pins in effect (a board preset or a
  checked custom map, spec 023), not fixed GPIO numbers. Boot reads the saved settings, then sets EN
  low and both PWM low before anything else; until then the EN pull-down holds the driver off, as it
  does through reset, flashing and the boot loader. A pin set that breaks the pin rules, settings
  that can't be read, or a new device with no board chosen drive nothing: every candidate motor pin
  stays an input and motion is locked out until a valid set is saved and the device restarts. A pin
  change applies only at a restart and is refused while the roof is busy.
- Modified: III. Unsafe Ends Closed. The local rain contact (GPIO4) is removed; rain comes from
  SQMeter (its rain gauge, `reasonFlags` bits 9 and 10, or its MQTT safe topic).
- New risk (III): rain is seen through SQMeter alone. A gauge that misses rain while SQMeter
  stays reachable, fresh and reports no sensor fault is no longer backed by a local contact. A
  SQMeter outage, a stale or unreadable reply, or a reported rain-sensor fault still make the
  verdict unsafe, exactly as before.
- New risk (I): EN is driven low a few tens of milliseconds later in the boot (after the settings
  are read). The driver's off state in that window rests on the EN pull-down, as it already did
  for the several hundred milliseconds of reset and boot loader before it.
- Modified: Platform Constraints. The reserved-pin table becomes the two board presets (ESP32
  DevKit; WT32-ETH01 on the RoofRoof carrier v1, with its Ethernet pins); one image for both.
- Why: spec 023. The roof's controller is a WT32-ETH01 on wired Ethernet, whose Ethernet uses the
  DevKit's motor pins, and the bench stays a DevKit; rain now comes from SQMeter's RG-15 gauge.
- Templates: no change (they read the principles from this file).
- Docs: docs/hardware/wiring.md, docs/user-guide/pins.md, docs/user-guide/safety-inputs.md,
  specs/023-wt32-eth01-board/spec.md.
- Earlier: 2.1.1 -> 3.0.0 (MAJOR: Principles II and III gain a test-box exception, which relaxes
  them).
- Modified: II. Close Only Onto a Parked Mount and III. Unsafe Ends Closed. On a box with no
  telescope under it and no weather station (a controller tried out on a desk), the owner may set
  test-box bypasses: a "no telescope" park source that stands in for N.I.N.A., and a weather bypass
  that stands in for a missing weather source. They exist only as explicit, visible, owner-set
  test-box settings: off by default, changed only by an authenticated settings save (with
  authentication off, as for every setting, any LAN client can make it), shown on every
  page, in the status, MQTT, Home Assistant, the Alpaca description and an alert, and ending at a
  deadline of at most 720 h that needs the clock. Principle III's step 2 (ask N.I.N.A. to park)
  becomes an owner setting, `park.requestPark`, on by default and after an upgrade: off, the
  unsafe close only waits for a park confirmed some other way (a bench box pointed at a live
  N.I.N.A. need not park that mount). Neither can be set alongside the source it stands
  in for (a N.I.N.A. address, an SQMeter source); fitted park switches still count (and, until
  4.0.0, the rain contact); no motion fault, the watchdog or the supply limit can be bypassed (Principle I unchanged).
- New risk (park requests): on a real roof with `park.requestPark` off, rain leaves the roof open
  unless the mount is parked some other way (by hand, or by N.I.N.A.'s own safety monitor); the
  critical "mount not parked" alert says no park was asked for, and the Mount park card warns
  while it is off with a N.I.N.A. address set.
- New risk: a telescope put under a roof still in test-box mode, with no N.I.N.A. address and no
  park switches, can be closed onto until the deadline. A box left in the open with the weather
  bypass on and no station is not closed for weather. Mitigations: the bypasses can't coexist with
  N.I.N.A. or SQMeter, so wiring up the real sources forces them off first; they expire (default
  72 h, at most 30 days); every surface shows them, and an alert goes out when one comes into effect
  and after every restart while one is in effect.
- Why: spec 022. Without it nothing that moves can be tried on a box with no mount and no station:
  jog, teach, the calibration run, Open and Close are all refused, and the bench build has no motor.
- Coordination: this is a MAJOR bump on top of 2.1.1 (spec 021). If spec 021's amendment is taken
  as 3.0.0 instead, this one becomes 4.0.0 with the same text.
- Templates: no change (they read the principles from this file).
- Docs: docs/user-guide/test-box.md, docs/user-guide/safety-inputs.md,
  specs/022-test-box-bypass/spec.md.
- Earlier: 2.1.0 -> 2.1.1 (PATCH: wording). Principle II's re-check during the close now says
  what it checks against: the latest good reading of each source while it is fresh. A N.I.N.A.
  reading that is good, fresh and parked stays in force across a failed read (no reply, an
  unreadable reply, an API error) until it is older than its freshness limit; a failed read is not
  a report that the mount moved. Any report of not parked, a fitted switch that opens or can't be
  read, or evidence past its freshness stops the motor as a motion fault, as before. The start
  rule ("all fresh at the moment the close starts", unreadable counts as not parked) is unchanged.
- Why: on the first board a single N.I.N.A. read timeout during a close latched the park
  interlock and left the roof part-way open (spec 021). The re-check bullet named its stop
  triggers (a mount that reports unparked, a switch that opens) but not what a failed read means;
  the code had read the start rule's "can't be read" into it.
- Risk stated: with no park switches fitted, a mount that unparks while N.I.N.A. stops answering
  can see the roof keep closing until the last good reading is 10 s old. Before, it stopped at the
  first failed reply: typically 2-3 s after that reading when N.I.N.A.'s host refuses the
  connection, up to about 9 s when the request times out (the poller's 2 s period and 6 s request
  limit). So the added exposure is typically about 7 s, at least about 1 s, and up to about 8 s. Park switches still
  stop it at once. Judged a clarification, not a relaxation: no
  reading is used past the freshness limit Principle II already sets, and every named trigger still
  stops the close at once; the owner may choose to treat it as MAJOR.
- Templates: no change (they read the principles from this file).
- Docs: docs/user-guide/safety-inputs.md (Park), specs/021-bench-resilience/spec.md.
- Earlier: 2.0.1 -> 2.1.0 (MINOR: Principle VII is expanded; nothing is relaxed).
- Modified: VII. Quiet, Consistent UI. The design system gains one documented set of onboarding
  components for guided flows (the modal dialog, the progress bar, the choice card group, the
  speech bubble and the mascot) in `web/src/components/ui.tsx`; beyond them, no new button or
  card styles. Any modal over the app carries the Stop bar inside it, so Stop stays visible,
  in the keyboard's focus loop and announced while the rest of the app is hidden behind it.
- Why: spec 018 turns the setup wizard into a one-question-per-screen dialog; built only from
  Settings cards and toggles, it would be the long form it replaces.
- Templates: plan-template.md, spec-template.md, tasks-template.md, checklist-template.md: no
  change (they read the principles from this file).
- Docs: docs/development/coding-standards.md gains DS-11 to DS-13 (the onboarding components) and
  DS-29 (a guided flow's question); specs/018-wizard-redesign/spec.md argues the amendment.
- Earlier: 2.0.0 -> 2.0.1 (PATCH: wording). Principle II's park rule now reads "N.I.N.A. reports
  AtPark AND every fitted park switch (park.switches) is closed". The park switches were always
  optional (the roadmap's design decision, park.switches none/one/two since spec 004, and
  ParkRule confirming on N.I.N.A. alone with none fitted); the old text named GPIO32/33 as if both
  were always fitted. Disagreement or an unreadable source still means not parked. No rule, and no
  behaviour, changes.
- Templates: no change. Docs: specs/ROADMAP.md's design decision, docs/user-guide/safety-inputs.md
  and the setup wizard's park inputs present the switches as an optional second check (spec 019).
- Earlier: 1.0.0 -> 2.0.0 (MAJOR: Principle I's timeout rule gains an exception, which relaxes it).
- Modified: I. Motion Safety, the hard-timeout rule. During an attended calibration run (spec
  016), before travel and speed are measured, a leg's hard timeout is roof.calibrationLimitSeconds
  (30-600 s), enforced by the motion code; later legs are bounded by the times already measured;
  the drive budget and every motion fault still apply.
- New risk: a leg's timeout no longer follows from the roof's own travel and speed. A leg that
  misses its magnet can drive into an end stop for up to the limit (at most 600 s, by default 180
  s) less the travel time; with current sense off, only the timeout stops it. Mitigations: only a
  person at the roof starts a run, every leg at the slow duty from an unknown position, the limit
  is bounded and the owner's setting, later legs are bounded by the measured ones.
- Templates: plan-template.md (Constitution Check) needs no change: it reads the principles from
  this file. spec-template.md, tasks-template.md, checklist-template.md: no change.
- Docs: docs/user-guide/calibration-run.md and specs/016-calibration-run/spec.md cite the rule.
- Earlier: 1.0.0 (initial). Derived from SQMeter's constitution 1.3.1 (SQMeter main @ 877208b).
- Added: I. Motion Safety and II. Close Only Onto a Parked Mount (no SQMeter equivalent);
  XI. SQMeter Heritage (the hybrid copy decision).
- Adapted from SQMeter: III (fail-safe safety inputs), IV (Alpaca, now a Dome), V-X.
- Templates: copied unchanged from SQMeter's .specify/ by spec 001.
-->
# RoofRoof Constitution

The firmware moves a roof over a telescope. A wrong move can crush a mount, and a missed close
can soak it, so the safety principles (I-III) come first. Where two principles conflict, the
lower-numbered one wins, and the controller stops the motor and raises a critical alert instead
of guessing.

## Core Principles

### Who May Set a Check Aside (10.0.0)

The owner is the source of truth. A safety check exists to stop the roof doing harm when nobody is
looking; a person who is looking can judge what a check can't (the camera shows the mount parked
while N.I.N.A. says it isn't).

- A **person** (authenticated at the web UI, or with the REST password) may be given a way past a
  safety check or a fault, for that one move: always with a warning that says what could happen
  and a deliberate confirmation, logged, and shown while in effect. Principles I-III name each one.
- An **automation** MUST NEVER get past a safety check, whatever it sends: an Alpaca client
  (N.I.N.A.), MQTT and Home Assistant, and the controller's own automatic moves. The only automatic
  exceptions are the owner-decided ones Principles I-III state, each off by default or bounded as
  it says: the automatic close from an unknown position and its blind close (I), the close when
  N.I.N.A. is lost (II), the Hall rail fault clearing itself at rest (I), a remote stop counting as a
  person's (`autoClose.remoteStop`, III), a person's stop lapsing (`autoClose.personStopLapse`, III,
  11.0.0), a move that started on a confirmed park going on when N.I.N.A. stops answering (II,
  12.0.0) and the test box's bypasses (II, III).
- The override bits are filtered by origin where a command is checked and where an override is
  kept, and a native test proves no automation's command carries one.
- A check a person can't yet get past is a gap, listed with the spec that found it (spec 039).
  Closing one relaxes Principles I-III and takes a MAJOR amendment that states the risk.

### I. Motion Safety

The motor moves only when the firmware knows where the roof is, why it is moving, and that
nothing has gone wrong.

- The driver MUST be off whenever the firmware is not deliberately driving it. EN low means off;
  both PWM outputs low means stop. EN has a hardware pull-down, so a reset, a crash, a brown-out or
  flashing leaves the driver off.
- EN, RPWM and LPWM are the pins in effect: a board preset, or a custom map that passes the pin
  rules in `lib/` (no reserved, input-only or strapping pin for EN; outputs only where the chip can
  drive). Boot reads the saved settings and then sets EN low and both PWM low before anything else
  runs; until then the EN pull-down holds the driver off, as through reset. Pins change only at a
  restart, and a change is refused while the roof is busy.
- A pin set that breaks the rules, settings that can't be read, or a new device with no board chosen
  MUST drive nothing: every candidate motor pin stays an input, and motion is locked out until a
  valid set is saved and the device restarts.
- RPWM and LPWM MUST NEVER be driven at the same time. A change of direction
  goes through stop (both low) and a dead time first. One function in one module sets the outputs,
  and a native test proves it can never produce both at once.
- The roof MUST NEVER move by itself while its position is unknown. Position is known only at a
  magnet, or estimated from time after leaving one in a move with no fault. Position is unknown
  after a boot away from a magnet, after an abort mid-travel and after a fault. Moving from an
  unknown position needs an explicit, authenticated command from a person, at reduced speed,
  stopping at the first magnet.
  - **The automatic close's exceptions** (owner decision, 8.0.0). Only the automatic close (unsafe
    weather, or daylight wanting the roof closed), only towards closed, only once the park is
    confirmed (Principle II), each ending with a critical alert that a person must check the roof:
    - with the Hall sensor working, it closes as a person's move from an unknown position does
      (slow duty, stops at the first magnet, the unknown-position timeout, every fault);
    - with the Hall sensor failed (a Hall rail fault latched and no other fault) and
      `roof.blindClose` on, a **blind close**: slow duty, towards closed on time alone, at most the
      unknown-position timeout, stopping on a stall seen as supply sag (`roof.blindSagVolts`), on any
      other fault and on Stop. The Hall fault stays latched. One blind close per latched fault,
      remembered across restarts. Risk: the chain is driven into its end until the stall stop or
      the time limit.
  - **A person's blind jog** (owner decision, 9.0.0). With the Hall rail fault the latched fault
    (and no other), or no fault and the Hall reading at a rail or not settled, a person's jog runs
    on time alone: slow duty, toward the end held, only while the button is held (the keep-alive
    deadline on the step clock), at most the jog's limit (30 s at most), stopping on release, Stop,
    Abort, a stall seen as supply sag and every fault still checked. The Hall fault stays latched
    and the position unknown. Any other fault still refuses it; only a person may start one. A
    person may also start one with the Hall sensor working, explicitly overriding the end magnets;
    a magnet read at rest afterwards doesn't place the roof until a move reaches one or new roof
    settings are saved. Risk:
    nothing stops the roof at its ends but the person watching, the keep-alive, the limit and the
    stall stop.
- Every move MUST have a hard timeout (the expected travel time from the measured travel and speed
  settings, plus a margin) and MUST stop on its target magnet. The timeout is enforced by the
  motion code itself, never by a client.
  - **The target magnet must be plausible** (9.1.0). For a move from a known position, its magnet
    counts only once the position estimate has covered `roof.endPlausiblePercent` of the travel
    toward it. Read earlier, the move continues at the slow duty and accepts it only if the reading
    lasts and becomes plausible, or is clearly at the magnet, within a few seconds; otherwise it
    stops with a motion fault and the position unknown, and that end's magnet doesn't place the
    roof until the other end is reached or the Hall settings are saved again. The state never
    reports that end meanwhile. Moves from an unknown position and calibration legs have no
    estimate to judge by and stop at the first magnet as before.
  - A move from a known position that has driven `roof.endOverrunPercent` of its expected time
    without its magnet stops then, before the hard timeout. Any move that shows no sign of
    movement (no supply sag of `roof.motionSagVolts`, no Hall change) for
    `roof.motionCheckSeconds` (twice that for a person's jog or a calibration leg) stops.
  - The one exception is an attended calibration run, which measures travel and speed. Before
    they are measured, each leg's hard timeout is the `roof.calibrationLimitSeconds` setting,
    bounded to 30-600 s and enforced by the motion code. Later legs are bounded by the times
    already measured. The drive budget and every motion fault below still apply, and each leg
    stops at the first magnet it reaches.
- Any motion fault MUST stop the motor, drop EN and lock out motion until a person clears it; the
  Hall rail fault's only exceptions are the blind close and a person's blind jog above.
  Motion faults include:
  - a timeout or "no magnet before the timeout";
  - an end magnet read too early, no end magnet where expected, or no movement seen (9.1.0);
  - over-current or a stall, when current sense is fitted;
  - the magnet for the wrong end;
  - a Hall reading at either rail (sensor disconnected). One that latched with the motor off
    clears itself after 60 s of readings off the rails with the roof at rest (also after a restart:
    the stored fault says it latched at rest), and the position is then unknown until the Hall reads
    a magnet; one that latched while driving waits for a person. A rail reading at rest in the first
    500 ms after start-up (the sensor and the ADC settling) isn't judged; no move can start then;
  - supply voltage below its limit;
  - a missed control-loop deadline.

  A restart never clears the lockout.
- Motion control runs in its own task at a fixed rate, independent of Wi-Fi, the web server, MQTT
  and alerts. If the task misses its deadline, the motor stops. A busy network can delay a command;
  it can never prolong a move.
- OTA updates, restarts, a config restore and settings that change motion MUST be refused while
  the roof is moving.
- The decisions behind every rule here MUST live in `lib/` with native tests covering each
  transition and boundary (Principle V). The hardware layer only applies what the logic decides.

### II. Close Only Onto a Parked Mount

Closing onto an unparked telescope is the most expensive failure this device can cause.

- The roof MUST NOT open or close unless the mount is confirmed parked by its park source
  (`park.source`), all fresh at the moment the move starts (a roof already at the end asked for has
  nothing to do and isn't checked):
  - **N.I.N.A. only**: N.I.N.A. reports `AtPark`;
  - **N.I.N.A. and park switches**: N.I.N.A. reports `AtPark` AND every fitted park switch
    (`park.switches`) is closed;
  - **park switches only**: every fitted park switch is closed (one or two; with two, both), and no
    fresh N.I.N.A. reading says the mount is slewing.

  If the sources disagree, or any of them can't be read, the mount counts as not parked. Settings
  saved before the source existed keep the rule they had (no switch: N.I.N.A. only; switches:
  N.I.N.A. and switches).
- **Park switches only.** The switches close to ground at the park position (normally open), so a
  broken wire reads not parked. A switch shorted to ground or stuck closed reads parked, so: two
  switches on separate cables are recommended and a single switch is warned about wherever the
  source is set; and a fitted switch that hasn't read open over 3 roof openings in a row is suspect
  and refuses Open until it has been seen open.
- **The telescope moves only with the roof at an end** (11.1.0). A slew, a park and an unpark are
  safe only while the roof is at rest on its closed magnet or its open magnet; part-open, moving,
  with the position unknown or a fault latched, the telescope hits the roof's beams, which is worse
  than rain. RoofRoof MUST NEVER trigger a slew otherwise: it asks N.I.N.A. to park (the automatic
  close, a command's park wait) only with the roof at an end, checked by one predicate in `lib/`
  where the request is decided and again where it is sent. With the roof not at an end and the
  mount not parked, nothing moves by itself: no park request, no close (onto an unparked mount),
  and a critical needs-a-person alert saying where the roof is ("Roof part-open (N %) with the
  mount not parked: moving the roof or the telescope would hit the beams. A person must check."),
  reminded until the roof is at an end, the mount is parked, or nothing wants the roof closed. With
  the mount parked, the closes Principle I allows (from an unknown position, blind) go ahead as
  before. The device tells imaging apps through its Alpaca Switch "Mount may move" and Dome
  `ShutterStatus` (open only at the open magnet), so a sequence can gate its own slews. A person
  may still move the roof or the mount through the confirmed paths below, warned; an automation
  never can.
- **A park request never stops a slew** (12.1.0). N.I.N.A.'s Advanced API stops a slewing mount
  before parking it, and a mount already parking reads slewing, so a second park request can leave
  the mount stopped part-way, not parked. RoofRoof MUST NOT send a park request while N.I.N.A.'s
  latest reading taken since the request (or, when N.I.N.A. can't be read, its last good reading
  within its freshness) says the mount is slewing: the request waits, and is decided on the next
  such reading. A reading that says parked ends it with nothing sent; one that says neither slewing
  nor parked (or none that can be read, as before) sends it once. Requests while one waits join it,
  and a request soon after RoofRoof's own send is decided only on a reading taken once the mount
  has had time to show that park. A slew still going after 180 s is not a park: the request is sent
  then, and logged. The roof must still be at an end when it is sent (above). N.I.N.A.'s answer to
  the request never confirms the park; only AtPark in a fresh reading does.
- If the park can't be confirmed, the roof MUST stay open and raise a critical alert. This holds
  even when it's raining: a wet telescope can be dried, a crushed one can't be fixed. The automatic
  close keeps asking for the park for as long as the roof should close, and the critical alert names
  the missing input ("N.I.N.A. unreachable, mount park unknown") and is reminded until a person
  resolves it.
- **Close when N.I.N.A. is lost** (owner-selectable, `autoClose.ninaLostClose`, off by default). With
  the park source N.I.N.A. or N.I.N.A. and switches, unsafe weather (not daylight alone) wanting the
  roof closed, and N.I.N.A. unreadable without a break for `autoClose.ninaLostMinutes`, the automatic
  close, and only it, may close without N.I.N.A.'s park: with N.I.N.A. and switches only while every
  fitted switch reads closed, which the re-check during that close keeps checking. A critical alert
  says the roof closed without N.I.N.A.'s park. Risk: with N.I.N.A. as the only park source nothing
  confirms the park, and the roof can close onto an unparked mount.
- Every open and close request is checked the same way, whatever its source: Alpaca
  `OpenShutter` and `CloseShutter`, the web UI, MQTT, Home Assistant or the automatic close. A request
  refused only for the park waits for it when N.I.N.A. can be asked (Principle III's steps 2 and 3,
  with their time limit and critical alert) and moves only once the park is confirmed; otherwise it
  is refused saying why. No path skips the check, except a person's confirmed close and confirmed
  open (below).
- **A person's confirmed close.** A person (authenticated), warned that the mount is not confirmed
  parked and the roof could hit the telescope, may confirm one close without the park check, at its
  start and during it. Alpaca, MQTT, Home Assistant and the automatic close never can.
- **A person's confirmed open** (owner decision, 10.0.0). A person (authenticated), warned that
  the mount is not confirmed parked and the roof passes over the telescope (check the camera), may
  confirm one open without the park check, at its start and during it: the park interlock doesn't
  stop that open, while the weather, daylight, every fault and Stop still do. It is logged and
  raises an information alert. Alpaca, MQTT, Home Assistant and the automatic close never can.
  Risk: an open onto a mount that isn't parked can hit the telescope, and nothing stops it for the
  park.
- **A person's jog toward closed** (owner decision, 9.0.0). A person at the roof, holding a jog
  toward closed (Principle I's hold to run, slow duty, the jog's limit), needs no confirmed park,
  at its start or during it, and is warned "Mount not confirmed parked: watch the telescope": they
  can see the telescope and let go. A jog toward open keeps the park check and the interlock.
  Alpaca, MQTT, Home Assistant and the automatic close never can. Risk: with the motor direction
  setting wrong, "toward closed" drives the roof open over an unparked telescope until the person
  lets go.
- The park interlock is re-checked throughout an open and a close (not a person's confirmed close
  or open). A mount that reports unparked (a fresh N.I.N.A. answer: not at park, slewing, mount not
  connected), N.I.N.A. and the switches disagreeing, or a fitted park switch that opens or can't be
  read, stops the motor as a motion fault, named by the move's direction ("Park lost during the
  open"). The re-check uses each source's latest good reading. With the park switches only, no
  N.I.N.A. reading is relied on: the switches are read every control step.
- **A command is a command** (owner decision, 12.0.0). A move that started with the park confirmed
  by a fresh reading (and every fitted switch closed) goes on to its end when N.I.N.A. merely stops
  answering: no reply, the network down, an unreadable reply, an API error, or its last reading
  past its freshness. N.I.N.A.'s silence is not a report that the mount moved, and a roof stopped
  part-way is the worst place for it (the rule above: neither the roof nor the telescope may move
  there). It goes on only on N.I.N.A.'s last answer having said parked, with the switches as read
  now; it still stops on a fresh answer that says "not parked", a switch, any fault, Stop, a stalled
  hand-over of the safety inputs (5 s), and for an open the weather and daylight. It never starts a
  move: every start needs the park confirmed by a fresh reading, as before. A move that finishes this
  way is logged, and raises an information alert ("Finished the close without a fresh park reading
  (N.I.N.A. unreachable)"). Risk: if the mount is unparked (a person, or N.I.N.A.'s own sequence,
  unparks it) while N.I.N.A. can't be read, nothing but the park switches sees it, and the roof can
  go on over a telescope that is no longer parked; with N.I.N.A. as the only park source nothing
  sees it at all.
- **Test-box exception.** On a box with no telescope under it, the owner may set the park source
  to "no telescope". Bypasses exist only as explicit, visible, owner-set test-box settings: off by
  default, changed only by an authenticated settings save, shown on every page and every interface
  while in effect, and ending at a deadline the device enforces. "No telescope" stands in for
  N.I.N.A. only: it never sits beside a N.I.N.A. address (the save that turns it on removes the
  address, and an address can't be saved while it is on), it can't be set with the park switches only,
  fitted park switches still count, and every close path uses the same park result. The persistent
  banner and the status satisfy "visible"; the alert announcing a bypass is an ordinary alert that
  the owner's pause and master switch hold back.

### III. Unsafe Ends Closed (Fail-Safe Safety Inputs)

Doubt about the weather MUST resolve to "unsafe".

- Unsafe or raining MUST always end with the mount parked and the roof closed:
  1. stop the running sequence;
  2. ask N.I.N.A. to park (when an address is set, unless the owner turned park requests off,
     `park.requestPark`);
  3. wait for the park to be confirmed (Principle II);
  4. close.

  Every step has a timeout. A step that times out stops the sequence where it is and raises a
  critical alert naming the step.
- **The weather hold** (8.0.0). The verdict turns unsafe at once, but the sequence starts only once
  the unsafe time, added up and forgotten only after a safe spell as long, reaches
  `autoClose.unsafeHoldSeconds` (default 60 s, 0-300 s); once started, a safe spell shorter than the
  hold doesn't call it off. Rain starts it at once, and so does a failed rain sensor (10.1.0: a rain
  sensor that fails may be failing because it is wet); it keeps its own reason, never reported as
  rain. A missing, stale or unreadable source is not a failed rain sensor and is held. A person's
  Open override in effect empties the hold. Risk: weather other than rain or a failed rain sensor,
  and a silent source (its stale time plus the hold), close later than they would without the hold.
- The weather verdict comes from SQMeter: `GET /api/safety` (its `safe` flag and `reasonFlags`,
  bit 9 rain and bit 10 rain-sensor fault) and/or its retained MQTT `<base>/safe`. Any source that says unsafe makes the verdict unsafe. A
  missing, stale or unparseable source counts as unsafe, and so does a rain sensor that has failed.
- Rain from any source MUST be acted on even if every other input is stale.
- **Park requests are an owner setting** (`park.requestPark`, on by default and after an
  upgrade). Off, step 2 is skipped and the sequence only waits for a park confirmed some other way;
  without one the roof stays open and the critical alert names the setting. The setting MUST be
  shown on its settings card and wherever a test-box bypass is shown.
- Losing every way to confirm the park while unsafe leaves the roof open and raises a critical
  alert. The alert path MUST NOT depend on the device that failed where another path exists.
- The roof MUST NOT open by itself. Opening is always a person's or a client's command, and it is
  refused while the verdict is unsafe, except a person's confirmed open.
- **A person's confirmed open.** A person (authenticated) may open past daylight, past the
  weather source's own report of unsafe for anything but rain, and (10.0.0, Principle II) past the
  park, naming each reason after they are listed. Rain, a failed rain sensor and any doubt about a source (missing, stale, unreachable,
  unreadable, offline) can't be overridden. The override lasts until the roof reads closed, a close
  is accepted, the overridden reasons clear, or a reason not overridden appears; meanwhile those
  reasons neither stop the open nor start the automatic close, and everything else does. It is shown
  on every interface while in effect. Alpaca, MQTT and Home Assistant never override.
- **A person's jog toward closed** (9.0.0). A close always wins: a person's jog toward closed (also a
  blind one, Principle I) is never refused or stopped by the weather, daylight or the automatic close
  waiting, standing down or unable to move; a jog toward open keeps every gate. The automatic close
  still takes over from a jog when it moves the roof itself.
- **A person's stop.** A person's Stop during the automatic close stands it down, so the close
  never fights someone at the roof; it raises a critical alert. The stand-down ends, and the close
  is armed again, when the roof reads closed, when the weather has been safe for the re-arm spell,
  when a person at the roof releases it while nothing wants the roof closed, or at once when an
  Open (a person's or a client's) starts the roof moving. Ending it never moves the roof.
  - **The stop may lapse** (11.0.0, owner setting `autoClose.personStopLapse`, spec 044). `never`
    (the default, and after an upgrade): the stand-down never lapses while unsafe. `rain`: it also
    ends once it has rained (or the rain sensor has failed) for `autoClose.stopHoldMinutes` (2-60
    min), added up since the stop and forgotten after a break as long; `always`: after that long of
    any unsafe weather. A new Stop starts the count over. A lapse MUST be logged and raise a critical
    alert, and the close then MUST go through every step and gate as any other (park request, park
    confirmed, close). A halted close and the stop of a close only daylight wants never lapse. While
    it counts, the status MUST show the time left. Risk: the roof can start closing minutes after a
    person stopped it, with that person still at the roof.
- Alerts, MQTT and the web UI report the verdict. They MUST never be able to change it, or the
  motion decisions taken from it.
- Every unsafe reason, fault and refusal MUST state the measured value and the limit, or the
  input that failed (`Supply 10.9 V < 11.5 V`, `Park switch open`, `N.I.N.A. not responding`).
- **Test-box exception.** With no weather source configured at all, the same owner-set test-box
  settings (Principle II) may bypass "no weather source". They never bypass a reading (a source
  that reports unsafe or rain) or the doubt of a source that is configured, and can't be set
  while an SQMeter source is on.

### IV. Standards Conformance (ASCOM Alpaca Dome)

N.I.N.A. drives the roof as an Alpaca Dome, and it must behave exactly as the specification says.

- The Alpaca HTTP API lives in `lib/AlpacaLogic` (copied from SQMeter, Principle XI), and the
  firmware and `tools/alpaca-sim` MUST both use it, never a second implementation.
- The Dome implements `OpenShutter`, `CloseShutter`, `AbortSlew`, `ShutterStatus` and `Slewing`,
  with `CanSetShutter` true. Every other capability reports false, and its methods return
  NotImplemented. The advertised interface version MUST match what is implemented.
- The Switch (11.1.0) is read-only: its switches report the roof at an end ("Mount may move") and
  at each magnet; `CanWrite` and `CanAsync` are false, and writing, renaming or the asynchronous
  members return NotImplemented. No Alpaca client can change a safety decision through it.
- `CloseShutter` and `OpenShutter` return at once and report progress through `ShutterStatus`.
  A move the safety rules refuse returns an Alpaca error saying why (Principles II and III). It
  never silently does nothing.
- Every change to the Alpaca code MUST pass ASCOM ConformU (conformance and the Alpaca protocol
  check) with zero errors, issues and configuration alerts, in CI against the simulator, for every
  device it offers.
- ConformU moves the shutter. It runs against real hardware only in bench mode (no motor) or with
  a person at the roof.
- The device UniqueID and discovery MUST stay stable across releases.

### V. Testable Pure Logic

Decision logic MUST be testable on a desktop without hardware.

- The roof state machine, motion profile, timeouts and fault detection, position estimate, Hall
  thresholds, the safety verdict, the close sequence, the park interlock and alert decisions MUST
  live in `lib/*Logic` without Arduino dependencies. Time and inputs come in as parameters, and the
  code is covered by native Unity tests (`pio test -e native`).
- Every safety rule in Principles I-III MUST have a test that would fail if the rule were broken,
  including timeouts, boundaries (`>=` vs `>`) and conflicting inputs.
- Web UI logic and components MUST be covered by Vitest tests, with pages exercised against the MSW
  mocks.
- A bug fix MUST add a test that fails without the fix, unless the fix is in untestable glue, in
  which case the PR states how it was verified instead.

### VI. Embedded Resource Budgets

- Firmware MUST build with `-Wall -Wextra -Werror` and zero warnings.
- The image MUST fit its OTA app slot (CI warns above 90%, fails above 95%; coding standard
  SIZE-04).
- Request handlers on the async TCP task MUST NOT block on the network or on hardware. Outgoing
  HTTP and HTTPS (SQMeter, N.I.N.A., alerts) runs on its own task, with one TLS session at a time.
- Changes that add heap or stack use MUST be measured on a device, keeping the main loop and motion
  task stack headroom above 2 KB.
- Persisted config MUST stay within its NVS limits and be validated before saving.
- The web UI is stored gzipped and MUST meet SIZE-01 and SIZE-02.

### VII. Quiet, Consistent UI

The web UI is used in the dark, often on a phone, sometimes in a hurry.

- The UI follows SQMeter's design system (DS rules, spec 026): the shared parts in
  `web/src/components/ui.tsx` and `settings/controls.tsx`, the tokens in `index.css`, and no new
  button or card styles.
  - The additions are the onboarding set for guided flows such as the setup wizard (DS-11 to
    DS-13): the modal dialog, its progress bar, the choice card group, the speech bubble and the
    mascot; and the banner for a state that holds on every page (DS-14). Each is built only from
    the tokens. A flow or page-wide state uses these rather than a style of its own.
- A safety check that a test-box bypass stands in for is never shown as passing on its own (OK,
  Parked, Safe): wherever it is listed it says it is bypassed.
- Stop MUST be reachable on every page while the roof is moving, and is never disabled.
  - A modal over the app MUST carry the Stop bar inside it, visible above its content, in its
    focus loop and announced when it appears.
- Open and Close show why they are unavailable (unsafe, mount not parked, fault lockout, position
  unknown) through a "?" or a Note. A greyed-out button never appears without its reason.
- Explanations go in "?" tooltips, and copy follows DS-20 to DS-28.
- Every page MUST work at phone width and meet WCAG 2.2 AA (A11Y rules), and every string goes
  through i18n (I18N rules).

### VIII. Trusted-LAN Security

The device is designed for a trusted LAN and has no TLS of its own.

- Every endpoint that moves the roof, clears a fault, changes state, sends something or reveals more
  than readings MUST call `requireAuth`.
- ASCOM Alpaca is open by the Alpaca design. It can move the roof, but only within Principles I-III.
- MQTT commands are honoured only from the configured broker and topics, and pass the same checks
  as every other command.
- Secrets (passwords, keys, tokens, including the N.I.N.A. credentials for API v3) MUST be masked
  in API responses and preserved when the masked value is sent back.
- Nothing may imply the device is safe to expose to the internet.
- The maintainer's private email address MUST NOT appear in any public file.

### IX. Docs Move With Behaviour

- A user-visible change MUST update `docs/` in the same PR: the user guide, the REST, WebSocket and
  MQTT references, and the configuration reference.
- The wiring and commissioning guides MUST match the pin table and the safety behaviour.
- Diagrams of the state machine and the close sequence follow SQMeter's diagram contract (DOC-01).
  They are safety diagrams, so a stale one fails the build.
- `mkdocs build --strict` MUST pass. API changes regenerate the OpenAPI document (API-01).

### X. Code Quality Standards

- All code MUST follow `docs/development/coding-standards.md`, copied from SQMeter's spec 017
  standard, with its rule IDs. Convergence findings and reviews cite them.
- `tools/quality/check.py` MUST pass in CI. The baseline starts empty and may only shrink.
- A rule is set aside only where it doesn't fit, with a same-line suppression naming the rule and
  the reason (EXC-01).
- Generated and vendored code is excluded and never edited by hand.

### XI. SQMeter Heritage

The platform is copied from SQMeter, not shared, because this project has a fixed end date (May
2027).

- Copied modules are listed in `docs/development/heritage.md` with the SQMeter commit they came
  from and whether they have diverged.
- A change to a copied module that would also help SQMeter is marked "upstream candidate" in that
  file. If the project outlives May 2027, those modules move to a shared library.
- Behaviour copied from SQMeter keeps SQMeter's names, field shapes and UI unless this project has
  a reason to differ, and the reason is recorded in the heritage file.
- SQMeter-specific features (sky, environment, wind, Bluetooth, OLED, battery, GPS) are not
  carried over.

## Platform Constraints

- **Firmware:** C++17 on Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5 (the pinned pioarduino platform;
  PlatformIO Core 6.2 or newer), ESP32 DevKit (WROOM, 4 MB) or WT32-ETH01 (wired Ethernet), one
  image for both.
  - ESP-IDF options live in `custom_sdkconfig`, and the build fails if one doesn't apply.
  - One build, one partition layout with two OTA slots.
  - ESPAsyncWebServer (handlers match by prefix, so register specific routes before general
    ones) and ArduinoJson 6.
  - Code style: enums and structs over strings, RAII, const-correctness, `std::string` in logic
    code.
- **Web UI:** Preact + Vite, TypeScript strict (no `any`), Zod validation of config, served from
  LittleFS. Firmware and web UI ship as a matched pair.
- **Hardware (board presets; spec 023):**

  | Function | ESP32 DevKit | WT32-ETH01 + RoofRoof carrier v1 |
  |---|---|---|
  | RPWM / LPWM | GPIO25 / GPIO26 | IO14 / IO15 |
  | R_EN and L_EN, joined, with a pull-down | GPIO27 | IO4 |
  | R_IS / L_IS current sense (optional, 10k/20k dividers) | GPIO34 / GPIO35 | IO35 / IO32 |
  | 12 V supply sense (100k/22k divider) | GPIO36 | IO36 |
  | SS49E Hall output | GPIO39 | IO39 |
  | park switches A / B (optional, internal pull-ups) | GPIO32 / GPIO33 | IO33 / IO5 |
  | Ethernet (LAN8720, reserved) | none | IO0, 16, 18, 19, 21, 22, 23, 25, 26, 27 |

  A custom map is checked by the same rules. Analog inputs use ADC1 only. Travel length, speed and Hall thresholds are settings, never
  constants.
- **Compatibility:** config saved by an older release MUST load and be migrated without a factory
  reset, and a migration MUST NOT turn on any motion feature that was off. The one exception is the
  blind close (`roof.blindClose`, Principle I), on after an upgrade by owner decision.
- **Updates:** OTA MUST be atomic, and is refused while moving (Principle I).

## Development Workflow and Quality Gates

- Work happens on a branch from `main` and lands through one pull request per feature or fix.
  Nothing is committed directly to `main`, and commits and PRs carry no AI co-author or
  attribution lines.
- A PR merges only when CI passes:
  - the build: typecheck, web tests, native tests, firmware build, LittleFS image, size budgets;
  - ConformU against the Alpaca simulator;
  - `tools/quality/check.py`;
  - the docs build.
- Motion and safety changes MUST be verified on the bench (bench mode, no motor) before they run
  on the roof. The first run on the roof is attended, and the PR records what was checked and
  how.
- Releases are cut by pushing a `v*` tag; `-beta.N` tags are pre-releases.
- New features follow Spec Kit: `/speckit-specify` → `/speckit-plan` → `/speckit-tasks` →
  `/speckit-implement`, repeating `/speckit-converge` until it reports converged. Specs live in
  `specs/<NNN-feature>/`.
- A spec touching motion or safety MUST list the Principle I-III rules it relies on and the tests
  that prove them.
- A spec that adds or changes UI cites the DS rules it relies on and is mocked up and reviewed by
  the owner before it is built. Mockup images stay outside the repo.
- The owner's prompts never go into the repository: specs, plans, tasks, commits and PRs are
  written in their own words.

## Governance

- This constitution takes precedence over other practice in this repository. `CONTRIBUTING.md` and
  the docs MUST NOT contradict it.
- Amendments are made by pull request editing this file, with the version bumped by semantic
  versioning:
  - MAJOR: a principle is removed or redefined incompatibly;
  - MINOR: a principle or section is added or materially expanded;
  - PATCH: wording and clarifications.
- Principles I-III can be relaxed only by a MAJOR amendment that states the new risk.
- `/speckit-plan`, `/speckit-analyze` and `/speckit-converge` check work against these principles.
  A plan that must break one records why in its Complexity Tracking section. Principles I-III
  admit no such exception.
- Each PR description notes which principles the change touches and how they were satisfied.

**Version**: 12.1.0 | **Ratified**: 2026-11-10 | **Last Amended**: 2026-11-26
