---
description: "The RoofRoof dashboard: the Status card and the Device & Network card, the alerts bell and the other pages. The roof's cards come in later specs."
---

# Dashboard

The dashboard is the web UI's home page, updated live over a WebSocket. Try it without hardware in the [live demo](../live-demo.md).

!!! note "The roof's cards come in later specs"
    This release is the platform only, so the dashboard has two cards: **Status** and **Device & Network**. The roof's position, controls and live telemetry *come in later specs*.

![Dashboard](../assets/screenshots/dashboard.png)

---

## Status

The **Status** card answers, without a click: will anyone be told when something happens. It looks like the other cards: tiles with a pill each, and a pill top-right that reads **All good** or **N to check**. It doesn't repeat what other cards show.

![Status card](../assets/screenshots/status-card.png)

| Tile | Shows | When it's there |
|---|---|---|
| **Alerts** | **Sending**, **Paused** with who paused it and a **Resume** button, **Waiting** for an imaging app, or **Off** | Always |
| **Imaging app** | One tile per Alpaca device: **Connected**, **Disconnected**, **Gone quiet** and when it last checked | Once an Alpaca device exists (*a later spec*) |

A problem no other card shows is one row under the tiles: its name, a pill, and "?" for what it means. The row takes you to where it's fixed.

| Row | Pill | When it's there |
|---|---|---|
| **Send mode** | **Not in effect**: "Only while an imaging app is connected" needs Alpaca, which is off, so alerts go out any time | That send mode with Alpaca off |
| **Alert channels** | **None on**, or **Can't send** (every channel that is on can't work right now, e.g. MQTT alerts with MQTT off) | When it happens |
| **Clock** | **Not set**: the time isn't known yet | When it happens |

Settings that are switched on but can't work are shown in **Settings**, where you set them: each reads **Not in effect** with the reason.

What's shown, and where each piece of device state appears (or why it doesn't), is recorded in `web/src/dashboard/inventory.json`, and every item has a test - see [Coding standards](../development/coding-standards.md#dashboard-dash). The card follows the UI design system ([DS rules](../development/coding-standards.md#ui-design-system-ds)).

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
| **Alpaca** | The ASCOM Alpaca devices N.I.N.A. sees, with live device state - see [ASCOM Alpaca](alpaca.md). None yet |
| **System** | Firmware, uptime, memory, flash partitions, time sync, MQTT, WiFi, and the restart action |
| **Settings** | Device, Network, Time and Alerts - see [Configuration](configuration.md) |
| **Updates** | Upload a firmware or web UI file - see [Updates](ota.md) |

![Alpaca page](../assets/screenshots/alpaca.png)

![System page](../assets/screenshots/system.png)
