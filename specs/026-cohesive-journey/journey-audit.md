# Journey audit: a new owner setting up the roof

Audit of the web UI as a first-time owner meets it, walked in the browser demo (the firmware core compiled to WebAssembly, so the roof, its checks and its refusals behave as on a device). The demo build walked was made from commit `345aa62` (main after PR #31); file and line references below are to that commit. Screenshots are kept outside the repository (constitution, Development Workflow: mockup images stay outside the repo); `screenshots/before/` and `screenshots/after/` name them relative to that folder.

Walked at 1280x800 and 390x844, dark theme: first boot, the setup wizard on both branches, the dashboard, every Settings tab, Pins, Calibrate (teach, jog, calibration run), Test box, System, Alpaca, Updates. The Demo panel's injectors were then used to create each refusal and fault, and two reported problems (MQTT following SQMeter, the test box weather bypass) were reproduced.

Screenshots are in `screenshots/before/`.

## 1. Map of the journey today

### Top navigation

Dashboard, Alpaca, System, Settings, Updates, alerts bell. Calibrate (`#/calibrate`), Pins (`#/pins`), Test box (`#/test-box`), the wizard (`#/wizard`) and Diagnostics have no navigation entry; each is reached only from a button inside another page.

| Page | Reached from |
|---|---|
| Calibrate | Settings, Roof, "Calibration run" card ("Open the calibration page"); Pins, Wiring check note ("Open Jog and teach"); the wizard's motor screens embed its parts |
| Pins | Settings, Roof, "Board and pins" card ("Open the pins page"); System, "Board and pins" card; dashboard Roof card "Fix the pin setup" (only with invalid pins); wizard board screen (Custom) |
| Test box | Settings, Roof, "Test box" card ("Open test box"); the red banner ("Manage"); dashboard Status card row; Settings, Roof, Mount park warning; wizard N.I.N.A. screen note |
| Wizard | Demo panel "Restart setup wizard"; System, "Setup" card, "Run setup wizard"; setup banner "Continue setup" when unfinished |

### Dashboard (`#/`)

- **Status** card: Imaging app, Alerts, and rows for problems (fault, "Automatic close: Held", "Test box: Bypassed"); header pill "All good" / "N to check".
- **Roof** card: state pill; Position; Open, Close, Stop; reason notes under the buttons ("Open: ...", "Close: ..."); notes ("Stop calls off the automatic close", "Position unknown: ..."); fault note with "Clear fault"; rows Weather, Mount park, Daylight (with location line), Automatic close (only when not idle); Supply; Motor current (if current sense on); Park switch 1/2 (if fitted); Hall reading with scale and level line.
- **Device & network** card: WiFi, Uptime, IPv4/IPv6, local name, MQTT, Firmware.
- Stop bar pinned to the bottom of every page while the roof moves or a close waits.

### Settings, Roof tab (`#/settings?tab=roof`), cards in order (`web/src/components/settings/RoofTab.tsx:413-429`)

1. **Board and pins**: Board preset (read only), "Open the pins page".
2. **Roof motion**: Travel (mm), Speed (mm/s), Timeout margin (s), Leave the magnet within (s), Ramp up (ms), Ramp down (ms), Top duty (%), Slow duty (%), Opening direction, Ask before Open and Close. Card hint (behind ?): "Measure the travel and speed on the roof. The hard timeout of each move is the expected time plus the margin."
3. **Hall sensor**: End that reads high, High threshold, Low threshold, live reading and scale.
4. **Calibration run**: text, "Open the calibration page", Advanced: Calibration time limit (s).
5. **Current and supply**: Current sense toggle (Max current, A/V, Min running current when on), Minimum supply (V).
6. **Weather sources**: SQMeter over HTTP (address, user name, password, Test), SQMeter over MQTT (topic, follows-SQMeter note), Stale after (s).
7. **Site from SQMeter**: SQMeter address, user name and password again when HTTP is off; "Copy from SQMeter".
8. **Mount park**: warning about a live N.I.N.A., N.I.N.A. address, Park switches fitted, May ask N.I.N.A. to park, Park time limit (s).
9. **Automatic close**: Safe spell to re-arm (min), Reminder interval (min), Close didn't start (Halt / Try again), Remote Stop calls off a close; Advanced: park request gap, park hold, back-off, close start, close guard, MQTT change gap.
10. **Daylight**: Daylight check (Off / Alert / Alert and close), Sun altitude limit, status line ("Inactive: Location not set", "Set location"), Location from (From SQMeter / Typed in), Location; Advanced: band.
11. **Test box**: mode pill, one line, "Open test box".

The other tabs: Device (Device, Security, Diagnostics, Backup and restore), Network (WiFi, MQTT with follows-SQMeter note, Roof commands, Home Assistant, ASCOM Alpaca), Time (Time zone, NTP; follows-SQMeter note), Alerts (Alerts, When to send, Notify me when, Roof safety, Roof, Imaging app, Channels with follows-SQMeter note on ntfy).

### Calibrate (`#/calibrate`, `web/src/components/Calibrate.tsx:318-361`)

Page title "Calibration run". Then:

1. **Teach the ends** card: intro, checklist (Weather safe, Mount parked, No fault latched, Automatic close idle, Supply above its minimum, Hall reading settled), "I am at the roof, watching it", Hall reading and scale, Hold to close / Hold to open, reason line ("Confirm that you are at the roof first" / "Not ready: ..."), "Where the roof is" (Closed end, Part-way, Open end), This is closed / part-way / open, proposal with Confirm and save, "Next, the calibration run below measures the travel times with these settings."
2. A loose paragraph between the cards: "The roof opens and closes at the slow duty ... proposes travel, speed, timeout margin and Hall thresholds."
3. **Start a run** card: the same checklist plus "Roof at rest", "I am at the roof and can see it", Where the roof is now, Measured travel (prefilled with the saved travel), Also run at the top duty, Start calibration, reason line.
4. While running, **Calibrating** card (leg n of m, "x s of at most y s", time limit meter, Stop calibration); afterwards **Last run** card with the proposal (Travel, Speed, Timeout margin, End that reads high, High/Low threshold) and Accept and save / Discard.
5. "Roof settings" link.

No Clear fault button on this page.

### Pins (`#/pins`)

Page "Board and pins": **Board choice** (preset, the pin list, Advanced), **Pins in effect** (validity, restart), **Wiring check** (Check wiring, results, note "Direction and travel need the roof to move. Open Jog and teach"), "Roof settings" link.

### Test box (`#/test-box`)

Title, intro; **Status** card (On/Off, Ends, Park check, Weather check, Daylight close, Turn off test box, Extend by N h); **Bypasses** card (No telescope under this roof, No weather station, No daylight close, each disabled with a reason and a link to the setting that blocks it; Turn off after (h); May ask N.I.N.A. to park; Turn on test box / Save, Discard). A red banner on every page while on.

### System (`#/system`)

Firmware, Runtime status, Flash partitions, NTP, MQTT, WiFi, **Board and pins** (again, with "Open the pins page"), **Setup** (status, "Run setup wizard"), Actions (Restart device).

### Alpaca (`#/alpaca`), Updates (`#/updates`)

Alpaca: endpoints, imaging app status, live dome state, "Alpaca settings". Updates: manual upload of firmware or web UI. Neither is part of the roof journey.

### Setup wizard (`web/src/components/wizard/screens.ts:60-82`)

| # | Screen | Question | Notes |
|---|---|---|---|
| 1 | name | What should we call this roof? | |
| 2 | network | How should RoofRoof join your network? | only with Ethernet |
| 3 | board | Which board is RoofRoof on? | |
| 4 | wiring | Does the wiring check pass? | |
| 5 | testBox | Is there a telescope under this roof yet? | "Yes, or there will be" / "No telescope yet (bench setup)" |
| 6 | sqmeter | Where is SQMeter? | address, login, Test, Copy from SQMeter, Weather from SQMeter, SQMeter over MQTT |
| 7 | site | Where is the observatory, and in which time zone? | hidden when site follows SQMeter |
| 8 | daylight | What if the roof is open after sunrise? | |
| 9 | parkSwitches | How many park switches are fitted? | |
| 10 | nina | Where is N.I.N.A. running? | hidden when the park bypass is on |
| 11 | direction | Does the roof open when you hold Open? | checklist, attended switch, jog |
| 12 | teach | Where are the roof's closed and open ends? | checklist, jog, marks |
| 13 | calibrate | Measure the roof with a calibration run? | start form |
| 14 | motion | Which values should the roof use for now? | only via "Enter manually instead"; Hall thresholds, Travel, Speed, Top duty |
| 15 | alerts | Where should alerts reach you? | |
| 16 | password | Protect changes with a password? | |
| 17 | review | Here's how your roof is set up. | 14 checks with Change links |

### Where each roof setting or action lives (duplicates in bold)

| Setting or action | Places |
|---|---|
| Board preset | **Settings Roof card; System card; Pins page; wizard board** |
| Wiring check | **Pins page; wizard wiring** |
| Opening direction | **Settings Roof motion; wizard direction ("Reverse it")** |
| Travel, Speed | **Settings Roof motion; wizard motion; Calibrate "Measured travel" (travel only); calibration proposal** |
| Timeout margin | **Settings Roof motion; calibration proposal** |
| Calibration time limit | Settings Roof, Calibration run card, Advanced only |
| Hall thresholds | **Settings Hall sensor; wizard motion; teach proposal; calibration proposal** |
| Live Hall reading | **Dashboard; Settings Hall sensor; Calibrate teach and run; wizard motor screens** |
| Jog | **Calibrate teach card; wizard direction and teach** |
| Teach | **Calibrate; wizard teach** |
| Calibration run | **Calibrate; wizard calibrate** |
| SQMeter address and login | **Settings Weather sources (HTTP on) or Site from SQMeter (HTTP off); wizard sqmeter** |
| Weather source on/off | **Settings Weather sources; wizard sqmeter ("Weather from SQMeter")** |
| Weather bypass | **Test box page; wizard testBox (all three at once)** |
| N.I.N.A. address | **Settings Mount park; wizard nina** |
| May ask N.I.N.A. to park | **Settings Mount park; Test box page; wizard testBox** |
| Park switches | **Settings Mount park; wizard parkSwitches** |
| Daylight mode | **Settings Daylight; wizard daylight; Test box "No daylight close"** |
| Location | **Settings Daylight card; Site from SQMeter copy; wizard site or sqmeter** |
| Automatic close timing (re-arm spell) | Settings Automatic close only |
| Clear fault | Dashboard Roof card only |
| Test box on/off | **Test box page; wizard testBox; banner (Manage); Settings card (link)** |

## 2. Confusing moments

Each entry: what was done, what was seen (verbatim), why it confuses a newcomer, and the screenshot.

### Why the roof won't move

1. **"Held" after Stop, explained only behind a "?"**. Roof open, Weather: Unsafe, the automatic close started ("Closing", "Stop calls off the automatic close"); pressed Stop on the dashboard; Weather: Safe. The dashboard shows "Automatic close: Held" in both the Status and Roof cards; the reason is only in the tooltip: "Someone pressed Stop: the automatic close stands down until the roof is closed or it has been safe for the re-arm spell." No duration, no countdown, no link to the "Safe spell to re-arm" setting (10 min), no way to end it. It lasted 598 s in the demo. `screenshots/before/1280-dashboard-held-by-person.png`, `screenshots/before/390-dashboard-held-by-person.png`, while closing: `screenshots/before/1280-dashboard-auto-close-closing.png`.
2. **Held blocks jog, teach and the calibration run with no meaning**. Same state, Calibrate: "Automatic close idle: Not yet" and under the disabled hold buttons "Not ready: Automatic close idle"; the run card says the same. Nothing says what "idle" means, that a Stop caused it, that it clears by itself after the re-arm spell, or how to clear it now. `screenshots/before/1280-calibrate-held-by-person.png`, `screenshots/before/390-calibrate-held-by-person.png`. The wizard's teach screen is identical: `screenshots/before/1280-wizard-teach-held-by-person.png`.
3. **Held blocks some moves and not others**. While held, the dashboard's Open and Close are enabled (verified: Open enabled), but jog, teach and the calibration run are refused. The newcomer can drive the roof from the dashboard but not from the page meant for setting it up, with no reason given for the difference.
4. **Two names for one thing**. The dashboard says "Automatic close: Held"; Calibrate says "Automatic close idle: Not yet"; Settings says "Safe spell to re-arm"; the tooltip says "re-arm spell"; the run note says "Stop calls off the automatic close"; Settings also has "Remote Stop calls off a close". A newcomer cannot connect them.
5. **Checklist rows have no reason, no "clears when", no fix**. Of the seven rows (`web/src/lib/calibrationView.ts:47-55`) only Weather, Mount parked and No fault latched carry a reason line; "Automatic close idle", "Roof at rest", "Supply above its minimum" and "Hall reading settled" show only "Not yet". With the supply low the dashboard says "Open: Supply 10.5 V is below 11.0 V" but Calibrate says only "Supply above its minimum: Not yet". `screenshots/before/1280-calibrate-supply-low.png`, `screenshots/before/1280-dashboard-supply-low.png`.
6. **"Not ready: <check name>" repeats the row instead of saying why**. "Not ready: Weather safe", "Not ready: No fault latched", "Not ready: Automatic close idle". The label of a condition that is false reads as if it were true ("Weather safe", then "Not yet"); "Not ready: No fault latched" reads as a double negative.
7. **Daylight is reported as weather**. Daylight mode "Alert and close", Sun up: Calibrate shows "Weather safe: Not yet" with "Sun 10.0° ≥ 0.0°" under it; the dashboard Weather pill stays "Safe" and the reason is "Open: Daylight (Sun 10.0° ≥ 0.0°)". `screenshots/before/1280-calibrate-sun-up-close-mode.png`, `screenshots/before/1280-dashboard-sun-up-close-mode.png`.
8. **A daylight bypass hides the weather row**. Bench branch of the wizard (only the daylight bypass took effect, see 24): the motor screens' first row is "Daylight close bypassed (test box): Bypassed" and there is no weather row at all, though the weather is still checked (`calibrationView.ts:16-29` replaces the weather label whenever a bypass is set and the check passes). `screenshots/before/390-wizard-bench-teach.png`.
9. **Raw device text as the reason**. Weather: Unsafe gives "Open: Weather not safe (unsafe: SQMeter HTTP says unsafe: reasonFlags 0x1)"; Rain gives "... reasonFlags 0x200 (rain)"; the checklist shows the same "unsafe: SQMeter HTTP says unsafe: reasonFlags 0x1". The word "unsafe" appears three times and the flag is a hex code. `screenshots/before/1280-dashboard-weather-unsafe.png`, `screenshots/before/1280-calibrate-weather-unsafe.png`, `screenshots/before/1280-wizard-direction-weather-unsafe.png`.
10. **The Status card says "All good" while the roof can't open**. With the weather unsafe, rain, SQMeter offline or SQMeter locked (roof closed), the Status card's pill stays "All good"; only the Roof card says why Open is refused.
11. **SQMeter offline or locked: no way to the fix**. "Open: Weather not safe (unsafe: SQMeter HTTP unreachable: connection refused (no good reply for 42.9 s > 30 s))" and "... SQMeter HTTP unreadable: SQMeter wants a password (no good reply for 90.9 s > 30 s)". Neither says where to enter SQMeter's login (Settings, Roof, Weather sources) or links there. `screenshots/before/1280-dashboard-sqmeter-offline.png`, `screenshots/before/1280-dashboard-sqmeter-locked.png`.
12. **Mount not parked: three ways of saying it in one line, and Open still allowed**. Calibrate: "Mount not parked: park not confirmed (not parked: N.I.N.A. says mount not at park)". On the dashboard (roof closed) Open is enabled with no note, while jog and calibration are refused for the same reason. `screenshots/before/1280-dashboard-mount-unparked.png`, `screenshots/before/1280-calibrate-mount-unparked.png`.
13. **A fault blocks Calibrate but can only be cleared on the dashboard**. Jam mid-travel: Calibrate shows "No fault latched: Not yet" with the fault text and "Not ready: No fault latched"; there is no Clear fault button on Calibrate or in the wizard's motor screens. `screenshots/before/1280-calibrate-timeout-fault.png`, `screenshots/before/1280-wizard-direction-jam-fault.png`.
14. **Reasons are spread over four renderers**. The dashboard (`web/src/lib/roofView.ts` reasons, `RoofCard.tsx` notes and pills, `glance.ts` Status rows), Calibrate and the wizard (`calibrationView.ts` checklist, `TeachCard.tsx` hold reason) and toasts (settings saves) each word the same cause differently, and each covers a different subset: the dashboard has no "held" reason, the checklist has no supply numbers, neither has a "clears when".

### Time limits and the calibration run

15. **"No target magnet after 65.9 s >= 65.9 s timeout" with no explanation**. Opened the roof and jammed it part-way (Jam on after 15 s). The fault reads "No target magnet after 65.9 s >= 65.9 s timeout" on the dashboard, the Status card ("1 to check", "Roof: Fault") and Calibrate. Nothing says the limit is travel / speed + ramps + timeout margin (1850 mm at 38 mm/s + 15 s here), that these are defaults, or that the calibration run measures and sets them. `screenshots/before/1280-dashboard-timeout-fault.png`, `screenshots/before/390-dashboard-timeout-fault.png`.
16. **The start-magnet fault names a setting that has another name**. Jam from the closed end: "Still on the start magnet after 10.0 s >= 10.0 s". The setting is "Leave the magnet within" in Settings, Roof motion. `screenshots/before/1280-dashboard-jam-fault.png`.
17. **Where move time limits come from is said once, in a tooltip**. Settings, Roof motion's hint (behind ?): "The hard timeout of each move is the expected time plus the margin." The Calibration run card, one card below, says "Measure travel time, speed and the Hall readings instead of typing them"; it does not say the run sets the time limits, and the run's own limit hides under Advanced as "Calibration time limit". The wizard says it only on the motion screen, which shows only after "Enter manually instead": "Travel and speed set each move's time limit; the calibration run measures them later." `screenshots/before/1280-settings-roof.png`, `screenshots/before/1280-wizard-observatory-motion.png`.
18. **Nothing on the dashboard or Calibrate says travel and speed are still the defaults**. Only the wizard's review says "Roof motion: Attention: Travel and speed are the defaults: measure them with the calibration run." A newcomer who skips or leaves the wizard is never told. `screenshots/before/1280-wizard-observatory-review.png`.
19. **"Measured travel" is prefilled with the default 1850 mm**. The field reads like a measurement already taken; starting the run with it unchanged gives a speed proposal from a made-up distance. `screenshots/before/1280-calibrate.png`.
20. **One page, three names**. The page is titled "Calibration run" but opens with "Teach the ends"; it is reached through "Open the calibration page" (Settings), "Open Jog and teach" (Pins) and leaves through "Roof settings". The teach card ends "Next, the calibration run below ..." while the run's description floats between the cards outside any card. `screenshots/before/390-calibrate.png`.
21. **The wizard lets the motor part be skipped without saying what is lost**. The teach and calibrate screens' Continue is enabled with nothing marked or measured; the only consequence shown is at the review ("Hall sensor: Attention", "Roof motion: Attention").
22. **The calibration run starting from the open end** (fixed on another branch, noted for the journey): the run's start form asks "Where the roof is now" with a guess from the Hall reading, and the dashboard, the teach card and the run card each show the Hall level differently ("At the low magnet", a number, a scale). A newcomer cannot tell which end "low" is without reading Settings, Hall sensor, "End that reads high".

### Where setup lives, and what is done

23. **No single place says what is set up and what is not**. Roof setup is in Settings, Roof (11 cards in one column), Calibrate, Pins, Test box, the wizard and the dashboard. Calibrate, Pins and Test box have no navigation entry. The only summary is the wizard's review, which is not reachable without re-running the wizard (System, Setup, "Run setup wizard"). `screenshots/before/1280-settings-roof.png`, `screenshots/before/390-settings-tab-roof.png`.
24. **The bench branch of the wizard does not make a bench**. Chose "No telescope yet (bench setup)" on a device with the demo's defaults. The screen warns "Bypasses the park, weather and daylight checks for 72 h", then "A N.I.N.A. address is set, so the park check stays on." and "A weather source is on, so the weather check stays on." Only the daylight bypass took effect. The wizard then still asks "Where is N.I.N.A. running?" with "Never point a bench box at a live N.I.N.A.", and the review says "Test box: Problem: Checks bypassed until ...". Making a bench requires leaving the wizard to clear N.I.N.A. and the weather sources in Settings. `screenshots/before/390-wizard-bench-testBox-chosen.png`, `screenshots/before/390-wizard-bench-review.png`.
25. **The review contradicts itself**. "Site: Problem: Site not set: the daylight check can't run." next to "Daylight: OK: The roof is closed after sunrise." (both branches, right after following SQMeter's location on the SQMeter screen, which said "The location comes from SQMeter."). `screenshots/before/1280-wizard-observatory-review.png`.
26. **First boot already "done"**. A fresh demo opens on the dashboard with setup marked done (System, Setup: "Done"); the Daylight row reads "Location not set / Waiting for SQMeter's location" until SQMeter is read, and Settings, Daylight shows "Inactive: Location not set" with "Location from: From SQMeter" and "Not read yet". A newcomer sees a problem that fixes itself a minute later. `screenshots/before/1280-first-boot.png`.
27. **Board and pins in three places**: Settings, Roof ("Board and pins" card), System ("Board and pins" card) and the Pins page, plus the wizard's board screen. `screenshots/before/1280-pins.png`, `screenshots/before/1280-system.png`.
28. **SQMeter's address and login move between cards**. With SQMeter over HTTP on they are in Weather sources; turning HTTP off moves them into "Site from SQMeter" in the same unsaved form. `screenshots/before/1280-settings-weather-source-off-preview.png`.
29. **"May ask N.I.N.A. to park" in three places**: Settings, Mount park; the Test box page's Bypasses card (where there is no N.I.N.A.); the wizard's test box screen. `screenshots/before/1280-test-box-weather-blocked.png`.
30. **Inconsistent terms for the same checks**: "Mount park" (dashboard), "Mount parked" (checklist), "Park check" (test box status), "No telescope under this roof" (bypass), "park check bypassed" (banner); "Weather" / "Weather safe" / "Weather check" / "No weather station" / "Weather sources" / "Weather from SQMeter"; "Daylight" / "Daylight check" / "Alert and close" / "Daylight close" / "No daylight close"; "Test box" / "bench setup" / "No telescope yet"; badges "OK" / "Not yet" / "Bypassed" (checklist), "Checked" / "Bypassed" (test box), "OK" / "Attention" / "Problem" (review); time limits "Timeout margin", "hard timeout", "time limit", "Calibration time limit", "Park time limit", "Leave the magnet within", "Close guard".
31. **Hall reading formatted two ways**: "2,050" on the dashboard, "2050" on Calibrate and in the wizard.

### Following SQMeter (reported problem 4)

32. **A typed MQTT login is replaced silently**. Settings, Network, MQTT (set here): typed user name "roofuser" and password "roofpass", pressed "Follow SQMeter" (the note became "Follows SQMeter once saved"; the user name field turned read-only but still showed "roofuser"), Save. Toast: "Saved." About 10 s later the saved user name was "observatory" (SQMeter's) and the password empty; the card showed "Follows SQMeter, checked 2 s ago" and "SQMeter's broker needs a password: enter it here." Nothing said the typed values had been replaced or that following would replace them. `screenshots/before/1280-settings-mqtt-follow-typed.png`, `screenshots/before/1280-settings-mqtt-follow-overwritten.png`.
33. **The password field stays editable while the rest is read-only**. While following, Broker, Port and Username are disabled but Password is not (`web/src/components/settings/NetworkTab.tsx`, MqttFields); a password typed for one's own user is cleared at the next sync when the user name differs from SQMeter's (`lib/SafetyInputLogic/src/SqmeterSync.cpp:48-53`).
34. **The only warning appears in a state most owners never see**. "Follows SQMeter once MQTT roof commands are off; what you type here is replaced then" shows only with roof commands on or no SQMeter address; in the normal following state there is no warning before a save.

### Test box weather bypass (reported problem 5)

35. **Two saves on two pages, with the roof closing in between**. A bench (N.I.N.A. cleared, park bypass on), roof open. The Test box page's "No weather station" toggle is disabled: "Off while SQMeter HTTP or MQTT is on." with a "Weather sources" link. In Settings, Roof, turned SQMeter over HTTP off; the save bar says only "Unsaved changes"; Save. Within 0.5 s the weather read "Open: Weather not safe (unsafe: No weather source: SQMeter HTTP and MQTT are both off)" and the automatic close started ("Automatic close: Closing"). `screenshots/before/1280-settings-weather-source-off-preview.png`, `screenshots/before/1280-dashboard-testbox-weather-source-off.png`.
36. **The bypass save is refused while that close runs**. On the Test box page, turned "No weather station" on, Save: toast "The roof is moving: stop it first, then try again". The Stop bar sat over the Save button at 1280x800 (a pointer click on Save was intercepted by the bar). `screenshots/before/1280-test-box-stopbar-over-save.png`, `screenshots/before/1280-test-box-on.png`, `screenshots/before/1280-dashboard-testbox-bypass-on-held.png`.
37. **Stopping it to save puts the roof into "held" and "Unknown"**. Pressed "Stop roof", then saved the bypass ("Saved."). Weather and park now read "Test box bypass", but the automatic close is "Held", the roof state and position "Unknown" (a save while stopped part-way), and Calibrate refuses jogging with only "Automatic close idle: Not yet" / "Not ready: Automatic close idle", for up to 10 minutes. `screenshots/before/1280-calibrate-testbox-bypass-held.png`. (Sending both changes in one settings save through the API did not blip: the weather stayed safe and nothing moved. The problem is the UI's two-step path.)

### Smaller things noticed

38. The Settings save bar never says what a save will do to a moving or open roof (weather becomes unsafe, an automatic close starts); "The roof is moving: saves are refused until it stops" shows only once it is moving.
39. After a jog ends, the wizard dialog keeps the stop bar's announcement text "Opening: Stop roof" in its accessible text on every later screen (seen in the dialog's text on teach, calibrate, motion, alerts, password and review).
40. At 390 px the Demo button covers the last row of the teach card and the Hall scale's level line (demo only). `screenshots/before/390-calibrate-held-by-person.png`.

Other screenshots taken: `screenshots/before/1280-dashboard.png`, `screenshots/before/390-dashboard.png`, `screenshots/before/1280-settings-{device,network,time,alerts}.png`, `screenshots/before/1280-test-box.png`, `screenshots/before/390-test-box.png`, `screenshots/before/390-pins.png`, `screenshots/before/1280-alpaca.png`, `screenshots/before/1280-updates.png`, `screenshots/before/1280-diagnostics.png`, and the wizard screens `screenshots/before/1280-wizard-observatory-*.png` and `screenshots/before/390-wizard-bench-*.png`.

## Information architecture proposal

### Principles

1. **One explanation of every refusal or hold.** Whatever stops a move is described once, in
   `web/src/lib/moveBlockers.ts`, and drawn once, by `MoveBlockers`: what stops it (with the device's
   value), what that means ("?"), when it clears, and its one fix (a page, **Clear fault** or
   **Release hold**). The dashboard, Calibrate (jog, teach and the calibration run) and the wizard's
   motor screens all use it, so the words never diverge and a check that is not blocking is not
   listed at all ("Nothing stops a move now.").
2. **One place that says what is set up.** The roof's setup stages, in the order an installer does
   them, each done or not with a link: board and wiring, weather check, park check, opening
   direction, ends taught, calibration run, alerts. On the dashboard as a card until every stage is
   done, and always at the top of Settings, Roof. Before the calibration run it says the move time
   limits are still defaults and how long a move is allowed.
3. **Settings in setup order, each setting in one place.** Settings, Roof runs top to bottom in the
   order of the stages: Roof setup, Board and pins, Weather sources, Copy from SQMeter, Mount park,
   Direction and speed, Hall sensor, Travel and time limits (with the move time limits beside travel
   and speed, who set them, and the calibration run's link and limit), Current and supply, Automatic
   close, Daylight, Test box. The separate Calibration run card goes (its link and limit live with
   travel and speed).
4. **A person's value is never silently replaced.** Typing in a group that follows SQMeter sets that
   group here, says so, and offers **Follow SQMeter again**; the device applies the same rule to any
   save.
5. **No two-step traps.** A change that needs two settings to move together (the test box's weather
   bypass and the weather source) is one save.

### Where each roof setting lives

| Setting or action | Home | Also reached from |
|---|---|---|
| Board, pins, wiring check | Pins page | Roof setup stage 1, Settings Roof card, System, wizard |
| Weather sources (SQMeter address, login, MQTT topic) | Settings, Roof, Weather sources | Roof setup stage 2, weather blockers' fix, wizard |
| Park source, N.I.N.A., switches, park time limit | Settings, Roof, Mount park | Roof setup stage 3, park blockers' fix, wizard |
| Opening direction, duties, ramps, leave-the-magnet time | Settings, Roof, Direction and speed | wizard's direction screen (direction only) |
| Hall end and thresholds | Settings, Roof, Hall sensor (typed) | Calibrate's teach (measured), wizard |
| Travel, speed, margin, move time limits, calibration limit | Settings, Roof, Travel and time limits | Calibrate (measured), Roof setup stage 6 |
| Jog, teach, calibration run | Calibrate | Roof setup stages 4 to 6, wizard motor screens |
| Release hold, Clear fault | the blocker that needs it (dashboard, Calibrate, wizard) | dashboard fault block |
| Test box bypasses | Test box page | banner, Settings card, wizard, "no weather source" note |

### What the dashboard says at each stage

| Stage | Dashboard |
|---|---|
| New device, no board | Roof setup card: board "To do" ("nothing is driven until then"); Roof card: the pin setup fault with **Fix the pins** |
| Board chosen, no weather source | Roof setup: weather "To do"; Open says why (weather check, its settings) |
| Sources set, nothing measured | Roof setup: direction, ends and calibration "To do", "Move time limits are still defaults (about 66 s a move)" |
| Measured, alerts off | Roof setup: alerts "To do" |
| All done | the Roof setup card goes; Settings, Roof keeps the list |
| A refusal or hold at any time | the Roof card's shared blocker list under the buttons; a stand-down under **Automatic close** with its clear time and **Release hold** |

### Before and after

The screens these findings came from are in `screenshots/before/`; the same scenes on the new
build are in `screenshots/after/` (dashboard, Settings Roof, the travel card, the dashboard and
Calibrate while held, the release, and Calibrate with the weather unsafe and the supply low).
