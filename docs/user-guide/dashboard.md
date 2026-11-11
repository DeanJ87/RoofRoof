---
description: "The RoofRoof dashboard: the Roof card with Open, Close and Stop, the Status card, Device & Network, Stop on every page while the roof moves, the alerts bell and the other pages."
---

# Dashboard

The dashboard is the web UI's home page, updated live over a WebSocket. Try it without hardware in the [live demo](../live-demo.md).

The cards are **Status**, **Roof** and **Device & Network**.

While the [setup wizard](setup-wizard.md) isn't done, a banner above the cards says so: **Setup isn't finished** with **Continue setup** on a new device (the wizard itself opens on the first dashboard visit after the device joins your WiFi), or an offer to **Run setup wizard** on a device upgraded from a release before it. **Dismiss** hides it for good.

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
| **Automatic close** | **Held** (someone pressed Stop during it), **Can't move** (a fault or an unknown position) or **Halted** (a close took too long) - see [Close on unsafe weather](close-on-unsafe.md) | While the automatic close is in one of those states |

Settings that are switched on but can't work are shown in **Settings**, where you set them: each reads **Not in effect** with the reason.

What's shown, and where each piece of device state appears (or why it doesn't), is recorded in `web/src/dashboard/inventory.json`, and every item has a test - see [Coding standards](../development/coding-standards.md#dashboard-dash). The card follows the UI design system ([DS rules](../development/coding-standards.md#ui-design-system-ds)).

## Roof

![Roof card](../assets/screenshots/roof-card.png)

The **Roof** card shows the roof as its control task sees it, live:

- the state as the pill top-right (**Closed**, **Open**, **Opening**, **Closing**, **Stopped**, **Unknown**, **Fault**), and the **Position** as a bar with the percentage open (**Unknown** until the roof reaches a magnet);
- **Open**, **Close** and **Stop**;
- with a latched fault, its text (the measured value and the limit) and **Clear fault**;
- **Weather** and **Mount park**: the weather verdict and the park check exactly as the roof's own command checks see them now, each with its reason behind "?"; and **Automatic close** while the [close on unsafe weather](close-on-unsafe.md) is under way or standing down;
- **Daylight**: the [daylight failsafe](close-on-unsafe.md#daylight-failsafe) as the device sees it: its mode and the sun's altitude against the threshold ("Sun 3.2° ≥ 0.0°"), or why the check can't run ("Location not set", "Clock not set"); with the mode off it says so;
- **Supply**, **Motor current** (while current sense is on), the park switches and the rain contact (when fitted);
- the **Hall reading** on its 0-4095 scale, with the low and high thresholds marked and where the reading is (at the low magnet, between the magnets, at the high magnet). Use it to [calibrate](../hardware/calibration.md).

### Open, Close and Stop

**Stop** is never disabled. It is sent the moment you press it, every time you press it, whatever the card shows.

With **Settings → Roof → Roof motion → Ask before Open and Close** on (it is off by default), **Open** and **Close** first ask "Open the roof?" or "Close the roof?" under the buttons; press the button there to send it, or **Cancel**. **Stop** never asks, and pressing it also dismisses the question.

**Open** and **Close** are greyed out when the device would refuse them, with the reason under the buttons:

| Reason | Open | Close |
|---|---|---|
| **No live data**: the connection to the device is down, or nothing has arrived for 5 s. The page won't move the roof on old news | yes | yes |
| A fault is latched: clear it first | yes | yes |
| The device is restarting (after it refused a command with `restarting`, until it is back) | yes | yes |
| Already at that end, or already moving that way | yes | yes |
| The weather isn't safe (the verdict's reason) | yes | - |
| It is daylight: the sun's altitude and the threshold (only with the daylight failsafe on **Alert and close**) | yes | - |
| The setup hotspot is on (it has no password) | yes | - |
| The mount isn't confirmed parked (what failed, e.g. a park switch open) | - | yes |
| A close is already waiting to start | - | yes |
| The supply is low or unreadable (the reading and the minimum) | yes | yes |
| The Hall reading hasn't settled yet | yes | yes |

The device checks every command anyway, whatever the page shows. If it refuses one, its reason appears under the buttons. When it is busy for a moment (an upload, a settings save or an MQTT connect is finishing) the buttons stay usable: try again shortly.

From an unknown position (after a restart between the magnets, or a fault) Open and Close still work: the roof moves slowly to the first magnet it reaches. While the automatic close is waiting for the park or closing, the card warns that **Stop** calls it off: a person's stop stands it down until the roof is closed or the weather has been safe for the safe spell to re-arm, 10 minutes by default (in daylight with **Alert and close**, until the sun has been down that long).

### Stop on every page

While the roof is opening or closing, or a close is waiting to start, a **Stop roof** bar is pinned to the bottom of every page. It stays if the live data stops while the roof was last seen moving.

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
