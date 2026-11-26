---
description: "The RoofRoof dashboard: the Roof card with Open, Close and Stop, the Status card, Device & Network, Stop on every page while the roof moves, the alerts bell and the other pages."
---

# Dashboard

The dashboard is the web UI's home page, updated live over a WebSocket. Try it without hardware in the [live demo](../live-demo.md).

Nothing on the dashboard is shown before the device has sent its first reading: until then the page says **Connecting**, with the Roof card under it (its state reads **Connecting**, Open and Close are unavailable, Stop always works). If the connection drops, or nothing arrives for 5 seconds (a phone that went to sleep), the page says **Reconnecting** instead of showing the old values, and reconnects at once when the page is back in front. The **System** and **Diagnostics** pages do the same. While the roof moves, the readings update twice a second, the same rate as the Diagnostics page; at rest every 2 seconds.

Explanations are behind **?**: hover, tap or tab to it. The tip is drawn over everything on the page and kept inside the window.

The cards are **Status**, **Roof** and **Device & Network**.

While the [setup wizard](setup-wizard.md) isn't done, a banner above the cards says so: **Setup isn't finished** with **Continue setup** on a new device (the wizard itself opens on the first dashboard visit after the device joins your WiFi), or an offer to **Run setup wizard** on a device upgraded from a release before it. **Dismiss** hides it for good. **Continue setup** resumes at the part you reached.

While the roof needs a person, a red banner on every page says so: **The roof needs a person**, with the device's own words for what and why (a restart with the position unknown or a fault latched, the Hall sensor failing, or the automatic close unable to move) and what to do. It goes when the condition ends (the fault is cleared, a magnet has been read after a move or a teach, the Hall sensor reads normally again), and the same condition is a Home Assistant binary sensor, **Needs a person** ([Alerts](alerts.md#roof-needs-a-person)).

![Dashboard](../assets/screenshots/dashboard.png)

---

## Status

The **Status** card answers, without a click: will anyone be told when something happens. It looks like the other cards: tiles with a pill each, and a pill top-right that reads **All good** or **N to check**. It doesn't repeat what other cards show.

![Status card](../assets/screenshots/status-card.png)

| Tile | Shows | When it's there |
|---|---|---|
| **Alerts** | **Sending**, **Paused** with who paused it and a **Resume** button, **Waiting** for an imaging app, or **Off** | Always |
| **Imaging app** | One tile per Alpaca device: **Connected**, **Disconnected**, **Gone quiet** and when it last checked | While Alpaca is on: the roof's Dome (not in the demo, which has no roof) |

A problem no other card shows is one row under the tiles: its name, a pill, and "?" for what it means. The row takes you to where it's fixed.

| Row | Pill | When it's there |
|---|---|---|
| **Send mode** | **Not in effect**: "Only while an imaging app is connected" needs Alpaca, which is off, so alerts go out any time | That send mode with Alpaca off |
| **Alert channels** | **None on**, or **Can't send** (every channel that is on can't work right now, e.g. MQTT alerts with MQTT off) | When it happens |
| **Clock** | **Not set**: the time isn't known yet | When it happens |
| **Roof** | **Fault**: a motion fault is latched; "?" says what happened, with the measured value and the limit. Clear it on the Roof card | While a fault is latched |
| **Test box** | **Bypassed**: a [test box](test-box.md) bypass is in effect, so the card never reads **All good**; the row opens the test box page | While a bypass is in effect |
| **Finish setup: …** | **N left**: the roof's setup isn't finished; the row opens the first stage left (see [Roof setup](#roof-setup)) | Until every setup stage is done |
| **Automatic close** | **Held** (someone pressed Stop during it), **Can't move** (a fault, or the roof controller didn't start) or **Halted** (a close took too long) - see [Close on unsafe weather](close-on-unsafe.md) | While the automatic close is in one of those states |

Settings that are switched on but can't work are shown in **Settings**, where you set them: each reads **Not in effect** with the reason.

What's shown, and where each piece of device state appears (or why it doesn't), is recorded in `web/src/dashboard/inventory.json`, and every item has a test - see [Coding standards](../development/coding-standards.md#dashboard-dash). The card follows the UI design system ([DS rules](../development/coding-standards.md#ui-design-system-ds)).

## Roof

![Roof card](../assets/screenshots/roof-card.png)

While a [test box](test-box.md) bypass is set, a banner above every page says which checks are bypassed and when it ends (red), or that they have expired or are waiting for the clock (amber). **Manage** opens the test box page.

The **Roof** card shows the roof as its control task sees it, live:

- the state as the pill top-right (**Closed**, **Open**, **Opening**, **Closing**, **Stopped**, **Unknown**, **Fault**), and the **Position** as a bar with the percentage open (**Unknown** until the roof reaches a magnet);
- **Open**, **Close** and **Stop** (see [Open, Close and Stop](#open-close-and-stop));
- with a latched fault, its text (the measured value and the limit) and **Clear fault**. A Hall reading at a rail with the roof at rest says it clears itself after a minute of good readings and shows the live reading; **Clear fault** still works at once;
- **Weather** and **Mount park**: the weather verdict and the park check exactly as the roof's own command checks see them now, each with its reason behind "?" (**Test box bypass** while a [test box](test-box.md) bypass stands in); and **Automatic close** while the [close on unsafe weather](close-on-unsafe.md) is under way or standing down;
- **Telescope**: **May move** while the roof is at rest at its closed or open magnet, **Keep still** otherwise (part-open, moving, position unknown or a fault): a slew, park or unpark would then hit the roof's beams, and RoofRoof won't ask N.I.N.A. to park. It is the same fact N.I.N.A. reads as the Alpaca Switch **Mount may move** ([Hardening your N.I.N.A. sequence](alpaca.md#hardening-your-nina-sequence));
- a note under **Weather** while the [weather hold](close-on-unsafe.md#the-weather-hold) counts ("Closing in 42 s unless it clears"): the seconds before the automatic close starts unless the weather clears (rain or a failed rain sensor starts it at once, with no countdown). Open is already refused and an opening roof already stopped;
- a note on the card while a close runs from an unknown position, or blind with a dead Hall sensor ([Close on unsafe weather](close-on-unsafe.md#closing-when-the-position-is-unknown)): the roof moves slowly with no position to show, and a person must check it afterwards;
- **Park wait** and **Override** lines while they apply, and the health of the two inputs: when **SQMeter** last replied and when **N.I.N.A.** last answered are behind the **Weather** and **Mount park** rows' "?". A silent SQMeter or an unreachable N.I.N.A. is a short note under its row ("No reply from SQMeter for 44 s", "N.I.N.A. not reachable"); the device's own reason (a refused connection, an HTTP status, an unreadable reply) is behind the same "?", and in full on the **Diagnostics** page;
- **Daylight**: the [daylight failsafe](close-on-unsafe.md#daylight-failsafe) as the device sees it: its mode and the sun's altitude against the threshold ("Sun 3.2° ≥ 0.0°"), or why the check can't run ("Location not set", "Clock not set"); with the mode off it says so. Below it, where the site came from and how old it is ("Location from SQMeter's GPS, read 3 min ago", "Typed-in location"), or, when SQMeter's location can't be read, which one is used ("Using the location saved 19 Nov"), with why behind the row's "?" ([Site from SQMeter](close-on-unsafe.md#site-from-sqmeter)); this line is information, never a safety verdict;
- **Supply**, **Motor current** (while current sense is on) and the park switches (when fitted);
- the **Hall reading** on its 0-4095 scale, with the low and high thresholds marked and where the reading is (at the low magnet, between the magnets, at the high magnet). Use it to [calibrate](../hardware/calibration.md).

### Open, Close and Stop

**Stop** is never disabled. It is sent the moment you press it, every time you press it, whatever the card shows. While the roof is still and nothing waits, the card shows **Open** and **Close** (never only Stop), with the reasons a move is blocked; **Stop** shows while the roof moves, a close waits to start, or a move waits for the park.

**The park wait.** When the mount isn't confirmed parked and N.I.N.A. can be asked, Open or Close doesn't refuse: the card says "Waiting for the mount to park", with the time left before it gives up and **Stop** to call it off, N.I.N.A. is asked to park, and the roof moves once the park has held ([Park before every move](close-on-unsafe.md#park-before-every-move)). If the wait ends without the move, the card says why.

**The move dialog.** When Open or Close would be refused, pressing it opens a dialog that lists every reason, each with its text:

- **Open** with only daylight, the weather SQMeter alone calls unsafe (not rain) and the park in the way offers **Open anyway**. It confirms each reason for this one opening, and the card then says the checks are overridden while it lasts. With the park among them the dialog warns in red that the roof passes over the telescope: check the camera first. An Open blocked only by the park goes at once and waits for the park, with **Wait for the park** and **Open anyway** (spec 039). If any reason can't be overridden (rain, a weather source in doubt, a fault, the supply), the dialog lists them all and offers no override.
- **Close** with the park not confirmed first asks N.I.N.A. to park when it can, and says the mount isn't confirmed parked and the roof could hit the telescope. **Close anyway** closes once without the park check; the next Close checks again.

An override is a person's, with the password: Alpaca, MQTT and Home Assistant never have one ([A person's confirmed open](close-on-unsafe.md#a-persons-confirmed-open)).

With **Settings → Roof → Direction and speed → Ask before Open and Close** on (it is off by default), **Open** and **Close** first ask "Open the roof?" or "Close the roof?" under the buttons; press the button there to send it, or **Cancel**. **Stop** never asks, and pressing it also dismisses the question.

When the device would refuse **Open** or **Close**, the buttons stay on the card with one short line under them saying the first reason in a few words ("Open: Weather unsafe", "Close: Mount not parked"; once, without the button's name, when both share it). The full reasons are in the move dialog a press opens: the shared **why it can't move** list (the same words as on Calibrate and the setup wizard), where each says what stops it, "?" says what that means, the line under it says when it clears, and its one fix is beside it (**Weather check settings**, **Park check settings**, **Supply settings**, **Fix the pins**). The detail of a reason is said once on the card, in its own row: the weather's in **Weather**, the park's in **Mount park**, a fault in its own block with **Clear fault**. A button that is greyed only because the roof is already at that end, or moving there, or held by a latched fault, gets no line: the state pill or the fault already says so, and the button's tooltip repeats it. The reasons:

| Reason | Open | Close |
|---|---|---|
| **No live data**: the connection to the device is down, or nothing has arrived for 5 s. The page won't move the roof on old news | yes | yes |
| A fault is latched: clear it first (said by the fault's own block) | yes | yes |
| The device is restarting (after it refused a command with `restarting`, until it is back) | yes | yes |
| Already at that end, or already moving that way (said by the state pill) | yes | yes |
| The weather isn't safe (the verdict's reason; **Open anyway** in the dialog when SQMeter alone says unsafe, not rain) | yes | - |
| It is daylight: the sun's altitude and the threshold (only with the daylight failsafe on **Alert and close**; **Open anyway** in the dialog) | yes | - |
| The setup hotspot is on (it has no password) | yes | - |
| A park switch hasn't opened in the last 3 roof openings (park switches only: it may be stuck or shorted) | yes | - |
| The mount isn't confirmed parked (what failed, e.g. a park switch open): pressing the button waits for the park when N.I.N.A. can be asked, else opens the move dialog | yes | yes |
| A close is already waiting to start | - | yes |
| The supply is low or unreadable (the reading and the minimum) | yes | yes |
| The Hall reading hasn't settled yet | yes | yes |

The device checks every command anyway, whatever the page shows. If it refuses one, its reason appears under the buttons. When it is busy for a moment (an upload, a settings save or an MQTT connect is finishing) the buttons stay usable: try again shortly.

From an unknown position (after a restart between the magnets, or an abort) Open and Close still work for a person: the roof moves slowly to the first magnet it reaches. The automatic close does the same, towards closed only, in unsafe weather. While the automatic close is waiting for the park or closing, the card warns that **Stop** calls it off: a person's stop stands it down until the roof opens or closes, or the weather has been safe for the safe spell to re-arm, 10 minutes by default (in daylight with **Alert and close**, until the sun has been down that long). While it stands down, the card says so under **Automatic close**, with the clock time it clears at while the weather is safe, and **Release hold** for a person at the roof ([Release hold](close-on-unsafe.md#release-hold)). Jog, teach and the calibration run wait for it too, and say the same.

## Roof setup

Until the roof is set up, the **Status** card has one row, **Finish setup: Weather check** (the first stage left) with a pill saying how many are left; "?" says what that stage needs, and the row opens the page that does it. It goes once every stage is done (a stage the test box bypasses is the **Test box** row's business). The full list is at the top of **Settings → Roof**: **Board and wiring**, **Weather check**, **Park check**, **Opening direction**, **Ends taught**, **Calibration run** and **Alerts**, each **Done** or **To do** (a test box bypass shows **Bypassed**; travel and speed typed rather than measured show **Not measured**), with what is left and a link to the page that does it. Before the calibration run it says the move time limits are still the defaults, with the time a move is allowed.

### Stop on every page

While the roof is opening or closing, or a close is waiting to start, or a move is waiting for the park, a **Stop roof** bar is pinned to the bottom of every page. It stays if the live data stops while the roof was last seen moving.

## Device & Network

| Card | Shows |
|---|---|
| **Device & Network** | WiFi signal and uptime as tiles; then one row each for IPv4, every IPv6 address (global, local, link, with "?" for the last two) when IPv6 is on, the `.local` name when mDNS is on, MQTT when it's on (**Connected**, **Retrying**, or **Can't connect** with the reason behind "?"), and the firmware version |

## Arranging cards

Cards flow into columns like a pin board. **Arrange** lets you drag them into your own order; **Done** keeps it and **Reset order** goes back to the default. The order is remembered per browser.

## Alerts bell

![Alerts flyout](../assets/screenshots/alerts-flyout.png)

The bell in the header lists the recent alerts with each channel's delivery result, and counts new ones. From it you can:

- **Pause** / **Resume** alerts - the same as **Pause alerts** in Settings (see [Alerts](alerts.md#when-to-send)); the bell is crossed out while alerts are paused
- **Clear** the list
- jump to the alert **Settings**

## Other pages

| Page | |
|---|---|
| **Alpaca** | The ASCOM Alpaca devices N.I.N.A. sees, with live device state - see [ASCOM Alpaca](alpaca.md). The roof's Dome; none in the demo |
| **System** | Firmware, uptime, memory, flash partitions, time sync, MQTT, WiFi, **Setup** (**Run setup wizard**) and the restart action |
| **Settings** | Device, Roof, Network, Time and Alerts - see [Configuration](configuration.md) |
| **Updates** | Upload a firmware or web UI file - see [Updates](ota.md) |

![Alpaca page](../assets/screenshots/alpaca.png)

![System page](../assets/screenshots/system.png)
