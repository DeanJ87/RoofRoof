---
description: "Test box mode: three owner-set bypasses with one deadline, for a RoofRoof box with no telescope or weather source yet. What each bypass stands in for, what is never bypassed, how the deadline works and how to turn it off."
---

# Test box mode

Test box mode is for a **real RoofRoof board with a motor** that is being tried before it has
its full surroundings: no telescope under the roof yet, no SQMeter yet, and perhaps a room with
the sun still up. Without it the roof would refuse to close (no N.I.N.A. means no confirmed park)
and refuse to open (no weather source means unsafe).

It is three separate switches, all off by default, and **all end at one deadline**.

!!! danger "Never leave it on with a telescope under the roof"
    A bypass means the roof stops asking questions. With the park bypass on, the roof closes
    without checking that a mount is parked. A telescope left under a roof in test box mode can be
    closed onto until the deadline. Turn the bypasses off before a telescope goes under the roof
    (see [Before it goes on the roof](#before-it-goes-on-the-roof)).

## The three bypasses

| Bypass | What it stands in for | When it is refused |
|---|---|---|
| **Park** | N.I.N.A.: it acts as a park source saying "no telescope under this roof", so a close is allowed | While a N.I.N.A. address is set, or with the park switches as the only park source (there is no N.I.N.A. to stand in for). Fitted [park switches](safety-inputs.md#park) still count: if they say the mount isn't parked, the close waits |
| **Weather** | A missing weather source: the "No weather source" verdict counts as safe | While SQMeter over HTTP or MQTT is on. A configured source that is in doubt (stale, unreachable, unreadable, says unsafe) is never set aside |
| **Daylight** | The sun: no [daylight close](close-on-unsafe.md#daylight-failsafe), and with **Alert and close** no refusal to open while the sun is up | Never refused. It only matters with the daylight mode on |

A save that would put a bypass beside the source it stands in for is refused with a message, for
example *"Test box: the park bypass is for a box with no telescope; clear the N.I.N.A. address
first (or turn the bypass off)"*.

## A test box and a live N.I.N.A.

!!! danger "A box pointed at a live N.I.N.A. can park that mount"
    Whenever the weather turns unsafe, a RoofRoof asks the N.I.N.A. at its address to park, test
    box or not. A bench box pointed at the N.I.N.A. that drives your real mount will park it
    mid-session. On a bench, leave the N.I.N.A. address empty, or turn off **May ask N.I.N.A. to
    park the mount** (`park.requestPark`, on by default; also offered on the wizard's test box
    screen). While a bypass is set, the banner says when unsafe weather would ask N.I.N.A. to
    park.

## What is never bypassed

- A latched fault, the watchdog, over-current, the Hall rail and move timeouts. During a close
  the park interlock still watches every fitted park switch.
- The 12 V supply check. There is no supply bypass: a board powered over USB reads about 5.5 V, so
  it stays "low". If you need that, use the [bench build](bench-mode.md).
- Any doubt from a source you have set up.

## The deadline

- One deadline covers all three bypasses. It is set by the **device**, not by you.
- It is stamped when you turn a bypass on, and again when you press **Extend**. **Turn off after**
  is 1 to 720 hours (default 72).
- It needs the **clock to be set** ([Settings → Time](configuration.md#ntp)). Without a clock a
  bypass can't be turned on, and one that is already set does nothing until the clock is set
  (the status says it is waiting for the clock).
- When the deadline passes, every check is back at once. The switches still show as set and the
  banner says **expired**. **Extend** stamps a new deadline.
- A [backup](configuration.md#backup-restore) restore never brings a deadline back. A restored
  bypass comes in with no deadline and does nothing until you extend it (the banner says **not
  started**).

## Where it shows

You can't miss it:

- a banner on every page of the web UI, under the header and in line with the page: red while a
  check is bypassed (**Test box: park check bypassed**, with when it ends), amber while it is
  set but every check applies (expired, waiting for the clock, not in effect, not started).
  **Manage** opens the test box page;
- `roof.testBox` in [`/api/status`](../api/rest.md#get-apistatus), `/ws/status` and the MQTT
  [state document](mqtt.md#the-roof-document);
- on the dashboard, the Weather and Mount park rows say **Test box bypass**;
- on the Calibrate page, the checklists say **Mount park check bypassed (test box)** (and the same
  for the weather and daylight checks) with an amber **Bypassed**, never **OK**. The check still
  passes, because the device allows the move;
- the Alpaca Dome's description starts `TEST BOX (bypassed: ...): `;
- Home Assistant has a **Test box bypass** sensor ([MQTT](mqtt.md#home-assistant));
- an alert, **Test box mode on**, goes out when a bypass comes into effect
  ([Alerts](alerts.md#the-roof)). It is an ordinary alert: **Send alerts** off or alerts paused
  hold it back, and a restart alone doesn't send it again. The banner and the status are what
  keep a bypass in view; its level never goes below Quiet;
- the log carries warnings.

## Turn it on

The test box has its own page. Open it from the banner's **Manage**, or from **Settings → Roof →
Test box** (**Open test box**); the setup wizard turns it on for a bench setup.

![The test box page while the park check is bypassed](../assets/screenshots/test-box.png)

- **Status** says whether it is on and, while it is, when it ends and which checks are
  **Bypassed** or **Checked**. **Turn off test box** turns every bypass off at once. **Extend by
  72 h** (your **Turn off after**) starts the deadline again from now.
- **Bypasses** has the three switches (**No telescope under this roof**, **No weather station**,
  **No daylight close**) and **Turn off after** (the hours). A switch that can't be turned on says
  why in one line, with a link to the setting in the way. **Turn on test box** (or **Save**)
  starts the deadline.

The [setup wizard](setup-wizard.md) can do the same: its test box screen, a part of its own after
the wiring check, asks whether a telescope is, or will be, under this roof. **No telescope yet
(bench setup)** turns on all three bypasses and skips the N.I.N.A. screen; the SQMeter screen then
supplies site data only. On a test box the direction, teach and calibration screens still move the
roof, attended, and each names the bypassed checks (faults, the supply and the time limits still
stop the roof).

Over the API, see [`POST /api/config`](../api/rest.md#post-apiconfig).

## Before it goes on the roof

1. Press **Manage** on the banner, then **Turn off test box**.
2. Set up N.I.N.A. (**Mount park**) and SQMeter (**Weather sources**), or run the
   [setup wizard](setup-wizard.md) again and answer **Yes, or there will be** on its test box
   screen (that turns every bypass off).
3. Check the dashboard: the banner is gone, and Weather and Mount park show real readings.

The device refuses a N.I.N.A. address or SQMeter while the matching bypass is on, so clearing the
bypass first is required.

## Not the same as bench mode

[Bench mode](bench-mode.md) is a separate firmware for a bare board: the roof is simulated and the
motor is never driven. Test box mode runs the standard firmware, with the motor, and only relaxes
the three checks above for a limited time.
