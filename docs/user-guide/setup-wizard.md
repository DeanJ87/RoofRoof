---
description: "The setup wizard: once RoofRoof has joined your WiFi, a dialog asks one question per screen about the essential settings, with live readings, tests of SQMeter and N.I.N.A., and a review of every check. It never moves the roof."
---

# Setup wizard

The first boot only joins your WiFi ([First Boot](../getting-started/first-setup.md)). The rest of
the essentials are spread over the **Settings** tabs. The setup wizard walks through them once, in
a sensible order, with the live readings each one needs, and ends with a review that lists every
check.

It opens as a dialog over the dashboard. A small dog asks one question per screen; you answer with
a choice card or a few fields, and a **Why this matters** panel (beside the question on a wide
screen, under it on a phone) says why it is asked.

![The setup wizard asking how many park switches are fitted](../assets/screenshots/wizard-choice.png)

!!! info "The wizard never moves the roof"
    No screen has Open, Close or Clear fault. The moves, the timeouts and the close-on-unsafe dry
    runs are done attended, with the motor disconnected first, in the
    [commissioning guide](../hardware/commissioning.md), which the last screen links. While the roof
    moves, the **Stop** bar shows at the bottom of the wizard, above everything in it, and the
    device refuses every save until it stops.

## When it opens

| Device | What you see |
|---|---|
| New, or after a factory reset, once it has joined your WiFi | The wizard opens the first time you open the dashboard, at the part you reached last. If you leave it, a banner on the dashboard says **Setup isn't finished**, with **Continue setup** and **Dismiss**. |
| Upgraded from a release before the wizard | A banner on the dashboard offers it (**Run setup wizard**, **Dismiss**). Nothing opens by itself. |
| Setup done (finished, skipped or dismissed) | Nothing. **System → Setup → Run setup wizard** runs it again at any time. |

The wizard doesn't open from the setup hotspot: joining the network is the hotspot's job.

## Moving through it

- **Continue** saves the screen (the same save, validation and password as **Settings**) and asks
  the next question. It stays unavailable, with a note saying what is missing, until the screen is
  answered: a choice made, a name or a password typed. A refused save keeps the screen open with
  the device's reason.
- **Skip for now** moves on without saving. **Back** (the arrow) steps back without saving. Either
  drops what you changed on the screen.
- The bar across the top shows how far along you are.
- On a new device, a question about your hardware starts with nothing chosen: say what is fitted.
  When you run the wizard again, your current settings are chosen.
- **Close** (the X) or **Escape** asks **Leave setup?**: **Keep going**, **Leave for now** (your
  saved answers stay; the dashboard offers to carry on) or **Skip setup**.

## The questions

The questions are grouped in the same eleven parts as before; the part you reach is what the
wizard resumes at.

1. **Device**: the name N.I.N.A. shows; then whether to protect changes with a password. **Set a
   password** is recommended and starts chosen when no password is set; **No password** warns that
   anyone on your network could change settings and move the roof. Set one before the motor is
   connected ([Security](security.md)).
2. **Site and time**: the latitude and longitude (for the daylight check); then the time zone and
   NTP.
3. **Hardware**, with the live readings:
    - the **12 V supply** as the device reads it through its fixed 100k/22k divider, and the
      minimum supply. Type what your meter shows at the driver's input: the screen says whether
      the two agree within 0.2 V. The meter reading is only compared, never saved;
    - whether **current sense** is fitted, and its scaling with the live motor current when it is;
    - how many **park switches** are fitted (none, one or two), each with its live state. Operate
      each by hand and watch it change;
    - whether a **rain contact** is fitted, with its live state.
4. **Hall sensor**: whether it is fitted. **Not fitted yet** leaves the thresholds as they are,
   says that without it the roof finds neither end (so the automatic close can't work), links the
   [quick bench test](../hardware/quick-bench-test.md), and skips the next screen. Otherwise, which
   end reads high and the two thresholds, with the live reading drawn against them as you type
   ([Calibration](../hardware/calibration.md)).
5. **Calibration run**: what the [calibration run](calibration-run.md) does and what it needs,
   with **Open the calibration page**, which leaves the wizard for the run's own attended page
   with Stop. Skip it until the motor is connected and its direction checked.
6. **Roof motion**: travel, speed and top duty. While they are still the factory values,
   **Use first-run values** sets the top duty to 40 %, the commissioning guide's first-move duty.
   Travel and speed are measured during commissioning (stage 8).
7. **Weather**: where the verdict comes from (**SQMeter's web address**, **SQMeter over MQTT** or
   **Both**); then the address or topic, with **Test**.
8. **N.I.N.A.**: the Advanced API address, with **Test**; then whether N.I.N.A. opens and closes
   the roof over Alpaca (**Yes, over Alpaca** is recommended). N.I.N.A.'s sequence drives the roof
   through the Alpaca Dome; RoofRoof is the failsafe behind it. Changing Alpaca needs a restart,
   which the last screen offers.
9. **Alerts**: where alerts should reach you (ntfy, Pushover, a webhook or MQTT); then that
   channel's details, with **Send test** once saved. With no channel, critical alerts (the roof
   didn't close, the mount didn't park) would reach no one.
10. **Daylight**: what to do with a roof open after sunrise: **Do nothing**, **Alert me**
   (recommended) or **Alert and close**, and the sun altitude threshold
   ([daylight failsafe](close-on-unsafe.md#daylight-failsafe)). Without the site's location the
   screen says so, and **Set location** saves your answer and goes back to the Site screen.
11. **Review**: one row per essential, each **OK**, **Attention**, **Problem** or **Unknown** (no
    reading yet), with what to do and **Change**, which opens that part. **Finish** ends setup
    and shows the last screen, with the commissioning guide.

![The setup wizard's supply screen with the live reading](../assets/screenshots/wizard-supply.png)

### Test

**Test** on the weather and N.I.N.A. screens sends nothing new: the device polls SQMeter every 5 s
and N.I.N.A. every 2 s anyway, and a saved change to an address is polled at once. Test saves the
screen if it has changes (as **Save and test**), then waits for the device's first reading of the
saved settings, for up to 15 s, and shows it:

- **Weather**: the verdict (**Safe**, **Unsafe** or **Unknown**) and each SQMeter source switched
  on (**Answering**, **Stale**, **Unreachable**, **Unreadable**, **Offline**) with the device's
  reason;
- **N.I.N.A.**: whether it answers (or why not), and the mount's **Mount connected**, **At park**
  and **Slewing** with the reading's age.

A reading from before the save is never shown as the result. The screen reads
[`GET /api/roof/inputs`](../api/rest.md#get-apiroofinputs).

![The setup wizard's N.I.N.A. screen after a Test](../assets/screenshots/wizard-nina.png)

## Skip and resume

- Each saved screen records the part you reached: close the browser and the wizard opens there
  the next time.
- **Skip setup** (after **Close** or **Escape**) warns that the roof may not be able to close itself in bad weather, or tell
  anyone, until the essentials are set, and ends setup only once you confirm. **Dismiss** on the
  dashboard banner does the same.
- **System → Setup → Run setup wizard** opens the first question with the current settings,
  whatever the setup state. **Leave for now** goes back to the System page.

## After the wizard

The last screen links the
[commissioning guide](../hardware/commissioning.md) and its
[gate for unattended use](../hardware/commissioning.md#stage-11-the-gate-for-unattended-use). The
wizard sets the settings of stages 2 to 5; the tests in every stage still apply.

The advanced settings (ramps, timeouts, the slow duty, the current limits, alert levels and wording,
MQTT and Home Assistant) stay in **Settings**.

## The setup state

`setup.state` (`new`, `offered` or `done`) and `setup.step` (the part to resume at, 0 to 10) are
ordinary settings ([Configuration](configuration.md#setup-wizard)). They move nothing and need no
restart. **Settings** never sends them, so a save there leaves them as they are.

## In the demo

The demo starts set up. **Demo → Restart setup wizard** sets it back to `new` and opens the wizard
([Live Demo](../live-demo.md)).

## Using it with a keyboard or screen reader

The wizard is a modal dialog named by its question. Tab stays inside it (the Stop bar included,
while the roof moves); the choices are a radio group the arrow keys move through; the progress bar
reads as "Screen 3 of 20". Each new screen moves focus to its question. Closing gives focus back
to where you were. With reduced motion set, nothing fades.
