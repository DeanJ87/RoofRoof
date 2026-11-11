---
description: "The setup wizard: once RoofRoof has joined your WiFi, eleven short pages walk through the essential settings with live readings, test SQMeter and N.I.N.A., and end with a review of every check. It never moves the roof."
---

# Setup wizard

The first boot only joins your WiFi ([First Boot](../getting-started/first-setup.md)). The rest of
the essentials are spread over the **Settings** tabs. The setup wizard walks through them once, in
a sensible order, with the live readings each one needs, and ends with a review that lists every
check.

!!! info "The wizard never moves the roof"
    No page has Open, Close or Clear fault. The moves, the timeouts and the close-on-unsafe dry
    runs are done attended, with the motor disconnected first, in the
    [commissioning guide](../hardware/commissioning.md), which the last page links. While the roof
    moves, the **Stop** bar is on the wizard's pages as on every other page, and the device refuses
    every save until it stops.

## When it opens

| Device | What you see |
|---|---|
| New, or after a factory reset, once it has joined your WiFi | The wizard opens the first time you open the dashboard, at the page you reached last. If you go back to the dashboard, a banner says **Setup isn't finished**, with **Continue setup** and **Dismiss**. |
| Upgraded from a release before the wizard | A banner on the dashboard offers it (**Run setup wizard**, **Dismiss**). Nothing opens by itself. |
| Setup done (finished, skipped or dismissed) | Nothing. **System → Setup → Run setup wizard** runs it again at any time. |

The wizard doesn't open from the setup hotspot: joining the network is the hotspot's job.

## The pages

Each page shows the settings it covers with the same controls, hints and ranges as **Settings**.
**Save and continue** saves the page (the same save, validation and password as **Settings**) and
opens the next; **Save** saves without moving on; **Skip page** and **Back** never save. A refused
save keeps the page open with the device's reason.

1. **Device**: the name N.I.N.A. shows, and the password. Password protection starts switched on
   when no password is set; turning it off warns that anyone on your network could change settings
   and move the roof. Set one before the motor is connected
   ([Security](security.md)).
2. **Site and time**: the latitude and longitude (for the daylight check), the time zone and NTP.
3. **Hardware**, with the live readings:
    - the **12 V supply** as the device reads it through its fixed 100k/22k divider. Type what
      your meter shows at the driver's input: the page says whether the two agree within 0.2 V. The
      meter reading is only compared, never saved; a difference points to the divider or its wiring;
    - **Current and supply**: current sense and its scaling, and the minimum supply;
    - **Park and rain inputs**: the park switches fitted, each with its live state, and the rain
      contact with its state. Operate each by hand and watch it change.
4. **Hall sensor**: which end reads high and the two thresholds, with the live reading drawn
   against them as you type ([Calibration](../hardware/calibration.md)). **Not fitted yet** leaves
   the thresholds as they are and says what that means: without the sensor the roof finds neither
   end, so the automatic close can't work. The
   [quick bench test](../hardware/quick-bench-test.md) shows how to try the controller without it.
5. **Calibration run**: what the [calibration run](calibration-run.md) does and what it needs,
   with **Open the calibration page**. The run is attended and has its own page with Stop; this
   page itself moves nothing. Skip it until the motor is connected and its direction checked.
6. **Roof motion**: travel, speed and top duty. While they are still the factory values,
   **Use first-run values** sets the top duty to 40 %, the commissioning guide's first-move duty.
   Travel and speed are measured during commissioning (stage 8).
7. **Weather**: SQMeter over HTTP and over MQTT, with **Test**.
8. **N.I.N.A.**: the Advanced API address, with **Test**, and the Alpaca Dome switch. N.I.N.A.'s
   sequence opens and closes the roof through the Alpaca Dome; RoofRoof is the failsafe behind it.
   Changing Alpaca needs a restart, which the last page offers.
9. **Alerts**: the master switch and the channels, with **Send test** per channel once saved. With
   no channel the page warns that critical alerts (the roof didn't close, the mount didn't park)
   would reach no one.
10. **Daylight**: **Off**, **Alert** or **Alert and close**, and the sun altitude threshold
   ([daylight failsafe](close-on-unsafe.md#daylight-failsafe)).
11. **Review**: one row per essential, each **OK**, **Attention**, **Problem** or **Unknown** (no
    reading yet), with what to do and **Change**, which opens its page. **Finish** ends setup.

![The setup wizard's hardware page](../assets/screenshots/wizard-hardware.png)

### Test

**Test** on the weather and N.I.N.A. pages sends nothing new: the device polls SQMeter every 5 s
and N.I.N.A. every 2 s anyway, and a saved change to an address is polled at once. Test saves the
page if it has changes (as **Save and test**), then waits for the device's first reading of the
saved settings, for up to 15 s, and shows it:

- **Weather**: the verdict (**Safe**, **Unsafe** or **Unknown**) and each SQMeter source switched
  on (**Answering**, **Stale**, **Unreachable**, **Unreadable**, **Offline**) with the device's
  reason;
- **N.I.N.A.**: whether it answers (or why not), and the mount's **Mount connected**, **At park**
  and **Slewing** with the reading's age.

A reading from before the save is never shown as the result. The page reads
[`GET /api/roof/inputs`](../api/rest.md#get-apiroofinputs).

![The setup wizard's N.I.N.A. page after a Test](../assets/screenshots/wizard-nina.png)

## Skip and resume

- Each saved page records where you are: close the browser and the wizard opens at the next page
  the next time.
- **Skip setup** warns that the roof may not be able to close itself in bad weather, or tell
  anyone, until the essentials are set, and ends setup only once you confirm. **Dismiss** on the
  dashboard banner does the same.
- **System → Setup → Run setup wizard** opens page 1 with the current settings, whatever the
  setup state.

## After the wizard

The review's **Next: commissioning** card links the
[commissioning guide](../hardware/commissioning.md) and its
[gate for unattended use](../hardware/commissioning.md#stage-11-the-gate-for-unattended-use). The
wizard sets the settings of stages 2 to 5; the tests in every stage still apply.

The advanced settings (ramps, timeouts, the slow duty, the current limits, alert levels and wording,
MQTT and Home Assistant) stay in **Settings**.

## The setup state

`setup.state` (`new`, `offered` or `done`) and `setup.step` (the page to resume at, 0 to 10) are
ordinary settings ([Configuration](configuration.md#setup-wizard)). They move nothing and need no
restart. **Settings** never sends them, so a save there leaves them as they are.

## In the demo

The demo starts set up. **Demo → Restart setup wizard** sets it back to `new` and opens the wizard
([Live Demo](../live-demo.md)).
