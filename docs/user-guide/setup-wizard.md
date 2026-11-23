---
description: "The setup wizard: once RoofRoof is on your network (WiFi or Ethernet), a dialog asks one question per screen, in an installer's order: the board and a wiring check, SQMeter, the mount park, then the roof's direction, teach and calibration run, attended, inside the wizard, then alerts, a password and a review of every check."
---

# Setup wizard

The first boot only joins your network: WiFi from the setup hotspot, or Ethernet on a WT32-ETH01 with a cable ([First Boot](../getting-started/first-setup.md)). The rest of
the essentials are spread over the **Settings** tabs. The setup wizard walks through them once, in
a sensible order, with the live readings each one needs, and ends with a review that lists every
check.

It opens as a dialog over the dashboard. A small dog asks one question per screen; you answer with
a choice card or a few fields, and a **Why this matters** panel (beside the question on a wide
screen, under it on a phone) says why it is asked.

![The setup wizard asking how many park switches are fitted](../assets/screenshots/wizard-choice.png)

!!! warning "The motor screens move the roof, attended"
    The wizard's direction, teach and calibration screens host the
    [Calibrate page's](calibration-run.md) attended jog, teach and calibration run, with every gate
    they have there: **I am at the roof** confirmed on each screen (it is never saved), the park and
    weather checks (or a [test box](test-box.md) bypass, which the screen names), the fault, supply,
    sequence and Hall checks, and the time limits. The device checks again when a move starts, and
    nothing moves unless you hold a button or start a run. While the roof moves, the **Stop** bar
    shows at the bottom of the wizard, above everything in it, and the device refuses every save
    until it stops. The moves of the later stages (the timeouts, the close-on-unsafe dry runs) are
    still done in the [commissioning guide](../hardware/commissioning.md).

## When it opens

| Device | What you see |
|---|---|
| New, or after a factory reset, once it is on your network (WiFi, or Ethernet on a WT32-ETH01) | The wizard opens the first time you open the dashboard, at the part you reached last. If you leave it, a banner on the dashboard says **Setup isn't finished**, with **Continue setup** and **Dismiss**. |
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

The questions are grouped in twelve parts; the part you reach is what the wizard resumes at. The
screens that don't apply to your setup are left out, so a real observatory sees at most 17 screens
and a test box 16.

1. **Network**: **What should we call this roof?** (the name N.I.N.A. shows); then, on a board with
   Ethernet, **How should RoofRoof join your network?**: *"You're on Ethernet at …"*, with the
   [network mode](network.md#the-network-mode) and an optional WiFi fallback. WiFi is optional
   there, so the screen can be skipped.
2. **Board**: **Which board is RoofRoof on?**: **ESP32 DevKit**, **WT32-ETH01 + RoofRoof carrier
   v1** or **Custom**, as choice cards. On a new device the one the device found at boot is
   preselected (the WT32 carrier when its Ethernet chip answered); you still confirm it. **Custom**
   shows the pin fields and the device's pin issues on this screen; only the dividers and trims
   are on the [Pins](pins.md) page, and **Open the pins page** saves the screen first and brings you
   back to this part. Until a board is saved and the device restarts, nothing is driven and every
   move is refused, so the screen offers the restart ([Pins](pins.md#a-new-device-no-board-chosen)).
3. **Wiring**: **Does the wiring check pass?**: **Check wiring** runs the
   [wiring check](pins.md#the-wiring-check) and shows each result (the EN pull-down, the Hall
   sensor, the supply, the park inputs, current sense at rest and Ethernet) with the value it read.
   It moves nothing, and is refused until the chosen pins are in effect (after the restart). The
   **Current sense** switch is here: turn it on once the driver's current outputs are wired. The
   supply is read as the device sees it; correcting it against a meter is on the Pins page
   (the supply calibration on the [Pins](pins.md) page).
4. **Test box**: **Is there a telescope under this roof yet?** **Yes, or there will be** turns every
   [test box](test-box.md) bypass off; **No telescope yet (bench setup)** turns them on (each
   only where its source isn't already set up, with a deadline) and warns never to use it with a
   telescope. The answer decides the two branches below.
5. **SQMeter**: **Where is SQMeter?** SQMeter's address and login are asked once, here (e.g.
   `192.168.1.50` or `sqmeter.local`; RoofRoof adds `/api/safety` itself), with **Test**, which
   reads SQMeter through the device, and **Copy from SQMeter**, which lists what SQMeter has that
   differs (time zone, NTP servers, MQTT broker, MQTT weather topic, ntfy) for you to tick and
   apply; **Continue** then saves them. The copied groups keep following SQMeter
   ([Keeping in step](close-on-unsafe.md#keeping-in-step); their fields show **Follows SQMeter**,
   with **Edit here instead**), and passwords and tokens are never copied
   ([Site from SQMeter](close-on-unsafe.md#copy-from-sqmeter)). Leave the address empty when there
   is no SQMeter. The screen also holds the weather source: the **Weather from SQMeter** switch
   (the roof opens only on SQMeter's safe verdict and closes when it turns unsafe) and **SQMeter
   over MQTT**, with its topic, and the broker's fields when MQTT is still off on the device.
    - What SQMeter supplies is not asked again. When the location and the time zone and time
      servers follow SQMeter, the screen lists each as done (*"The location comes from SQMeter."*,
      *"The time zone and time servers come from SQMeter."*) with **Edit**, which opens the site
      and time screen; with both done, that screen is skipped.
    - Otherwise **Where is the observatory, and in which time zone?** asks where the location comes
      from (**From SQMeter**, or typed in, for the daylight check), then the time zone and NTP.
    - On a test box with the weather bypassed, the screen keeps the address for site data only and
      says the weather check is bypassed.
    - **What if the roof is open after sunrise?**: **Do nothing**, **Alert me** (recommended) or
      **Alert and close**, and the sun altitude threshold
      ([daylight failsafe](close-on-unsafe.md#daylight-failsafe)). Without the site's location the
      screen says so, and **Set location** goes to the site screen.
6. **Mount park**: **How many park switches are fitted?** (**None**, **One switch** or **Two
   switches**), each with its live state: operate each by hand and watch it change. The switches
   are optional: N.I.N.A.'s park alone confirms the mount is parked, and fitted switches are a
   second check that must agree ([Safety inputs](safety-inputs.md)). Then, unless the park is
   bypassed, **Where is N.I.N.A. running?**: the Advanced API address, with **Test**, and whether
   N.I.N.A. opens and closes the roof (**Open and close over Alpaca**, recommended). N.I.N.A.'s
   sequence drives the roof through the Alpaca Dome; RoofRoof is the failsafe behind it. Changing
   Alpaca needs a restart, which the review offers. On a test box this screen is skipped, as the
   park bypass stands in for N.I.N.A.
7. **Direction**: **Does the roof open when you hold Open?** Confirm **I am at the roof**, hold
   **Hold to open**, and watch the roof and the live Hall reading. Answer **Yes, it opened** or
   **No, it closed**; the second says to swap the motor's two wires at the driver, and **Continue**
   stays unavailable until it is **Yes**. The answer is a confirmation, like **I am at the roof**: it
   is never saved.
8. **Teach**: **Where are the roof's closed and open ends?** The Teach card of the
   [Calibrate page](calibration-run.md#teach-the-ends): hold a direction, mark **This is closed**,
   **This is part-way** and **This is open**, then **Confirm and save** the proposed Hall settings
   or **Discard** them.
9. **Calibration**: **Measure the roof with a calibration run?** The [calibration run](calibration-run.md)
   itself: its checks, **I am at the roof and can see it**, where the roof is now, the measured
   travel, the optional top-duty pass, the run with its **Stop calibration** and its open-end
   question, and the proposal, which **Accept and save** saves through the settings save. The run
   keeps going under the device's own limits if you leave the screen, and the screen shows it
   (or its result) when it opens again.
    - **Enter manually instead**, on each of the three motor screens, opens one screen that asks for
      the numbers instead: the Hall end and thresholds (with the live reading drawn against them),
      the travel, the speed and the top duty. While they are still the factory values,
      **Use first-run values** sets the top duty to 40 %, the commissioning guide's first-move duty.
      Use it when there is no motor or sensor yet. **Continue** goes on to the alerts.
10. **Alerts**: **Where should alerts reach you?** Tick any number of channels (**ntfy**,
    **Pushover**, **Webhook**, **MQTT**) or **No alerts for now**, which unticks the others;
    unticking a channel turns it off. Each ticked channel's fields show on the same screen (filled
    in where you copied them from SQMeter), with **Send test** once saved. Ticking **MQTT** while
    MQTT is off turns MQTT on and shows the broker's fields there. A refused save shows each
    field's error under its field, and the note at the bottom names the fields to fix. With no
    channel, critical alerts (the roof didn't close, the mount didn't park) would reach no one.
11. **Password**: **Protect changes with a password?** Nothing is chosen until you answer. **Set a
    password** is marked recommended; **No password** warns that anyone on your network could
    change settings and move the roof. Whatever you saved stays as it was when you come back to
    the screen. Until you set one the device is open to your network ([Security](security.md)).
12. **Review**: one row per essential, each **OK**, **Attention**, **Problem** or **Unknown** (no
    reading yet), with what to do and **Change**, which opens the part that sets it. **Finish** ends
    setup and shows the last screen, with the commissioning guide.

There is no rain question: rain comes from SQMeter's rain gauge
([Safety inputs](safety-inputs.md#weather)).

### The two branches

| Screens | Test box | Real observatory |
|---|---|---|
| SQMeter | Weather bypassed: SQMeter supplies site data only | Weather from SQMeter |
| Mount park | Park switches only; the N.I.N.A. screen is skipped | Park switches and N.I.N.A. |
| Direction, teach and calibration | Park and weather checks bypassed, each screen naming them; faults, the supply and the time limits still stop the roof | Park and weather checks apply |

![The setup wizard's board screen](../assets/screenshots/wizard-board.png)

![The setup wizard's SQMeter screen](../assets/screenshots/wizard-sqmeter.png)

![The setup wizard's motor screen](../assets/screenshots/wizard-motor.png)

![The setup wizard's alerts screen with several channels ticked](../assets/screenshots/wizard-alerts.png)

### Test

**Test** on SQMeter's address (SQMeter screen) is SQMeter's own read through the device; the
verdict, the reason and each source's state are shown. **Test** on the N.I.N.A. screen sends nothing new: the device polls N.I.N.A.
every 2 s anyway, and a saved change to an address is polled at once. Test saves the screen if it
has changes (as **Save and test**), then waits for the device's first reading of the saved
settings, for up to 15 s, and shows it: whether N.I.N.A. answers (or why not), and the mount's
**Mount connected**, **At park** and **Slewing** with the reading's age.

A reading from before the save is never shown as the result. The screen reads
[`GET /api/roof/inputs`](../api/rest.md#get-apiroofinputs).

![The setup wizard's N.I.N.A. screen after a Test](../assets/screenshots/wizard-nina.png)

![The setup wizard's review](../assets/screenshots/wizard-review.png)

On a phone the dialog fills the screen, with the **Why this matters** panel under the question.

![The setup wizard on a phone](../assets/screenshots/wizard-phone.png)

## Skip and resume

- **Continue** and **Skip for now** both record the part the next screen belongs to. **Skip for
  now** saves nothing else.
- Reloading the page, closing the browser, the dashboard banner or coming back later opens the
  wizard at the part you reached, in every setup state, with your saved answers as you left them.
  **No password** stays chosen.
- **System → Setup → Run setup wizard** starts at the first part, once; a reload after that
  resumes where you were.
- The wizard leaves for another page only after saving the screen (**Open the pins page**, or a
  fix only **Settings** can make) and opens at the same part when you come back.
- **Skip setup** (after **Close** or **Escape**) warns that the roof may not be able to close itself in bad weather, or tell
  anyone, until the essentials are set, and ends setup only once you confirm. **Dismiss** on the
  dashboard banner does the same.

## After the wizard

The last screen links the
[commissioning guide](../hardware/commissioning.md) and its
[gate for unattended use](../hardware/commissioning.md#stage-11-the-gate-for-unattended-use). The
wizard sets the settings of stages 2 to 5; the tests in every stage still apply.

The advanced settings (ramps, timeouts, the slow duty, the current limits, alert levels and wording,
MQTT and Home Assistant) stay in **Settings**.

## The setup state

`setup.state` (`new`, `offered` or `done`) and `setup.step` (the part to resume at, 0 to 11: network, board, wiring, test box, SQMeter, mount
park, direction, teach, calibration, alerts, password, review) are
ordinary settings ([Configuration](configuration.md#setup-wizard)). They move nothing and need no
restart. **Settings** never sends them, so a save there leaves them as they are.

## In the demo

The demo starts set up. **Demo → Restart setup wizard** sets it back to `new` and opens the wizard
([Live Demo](../live-demo.md)).

## Using it with a keyboard or screen reader

The wizard is a modal dialog named by its question. Tab stays inside it (the Stop bar included,
while the roof moves); the choices are a radio group the arrow keys move through; the progress bar
reads as "Screen 3 of 17". Each new screen moves focus to its question. Closing gives focus back
to where you were. With reduced motion set, nothing fades.
