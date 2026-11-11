---
description: "A quick bench test of the RoofRoof controller, the IBT-2 and the 12 V motor before the SS49E Hall sensor exists: a potentiometer or fixed divider stands in for the Hall, with bench settings, a weather source and the park."
---

# Quick bench test without the Hall sensor

This page is for a first look at the real controller moving the real motor on the bench, when the SS49E Hall sensor and its magnets don't exist yet. An ESP32, an IBT-2, the 12 V worm motor and a weather source (an [SQMeter](https://github.com/DeanJ87/SQMeter)) are enough. A potentiometer, or two resistors, stands in for the Hall sensor.

!!! danger "This does not replace the commissioning guide"
    It is a quick look at the motor and the controller's responses, not a safety proof. It skips the stages that prove the roof is safe to leave alone: the Hall calibration ([stage 3](commissioning.md#stage-3-hall-calibration-with-the-live-reading-motor-disconnected)), the driver and fault tests ([stage 6](commissioning.md#stage-6-the-driver-and-the-faults-motor-disconnected)), the close-on-unsafe dry run ([stage 7](commissioning.md#stage-7-the-close-on-unsafe-dry-run-motor-disconnected)), the first attended moves on the real roof ([stage 8](commissioning.md#stage-8-first-attended-moves-on-the-real-roof)) and the unattended gate ([stage 11](commissioning.md#stage-11-the-gate-for-unattended-use)). Do the full [commissioning guide](commissioning.md) before the controller goes on the roof.

## Safety first

- The motor sits on the bench, **unloaded**, fixed so it can't jump, with a hand near the 12 V switch. A switch is the real stop; the software Stop is a second one.
- Know where **Stop** is: on the dashboard's Roof card, and the **Stop roof** bar pinned to the bottom of every page while the roof moves.
- Leave the Alpaca Dome and MQTT roof commands off (`alpaca.enabled` and `mqtt.roofCommands`, both off by default), so nothing but your own clicks in the web UI can move the motor.
- Fit and measure the EN pull-down before the first power-up, exactly as in [commissioning stage 1](commissioning.md#stage-1-wiring-and-the-en-pull-down-everything-off): with the ESP32 unpowered, 8 to 11 kΩ from GPIO27 to GND.

## Wiring

| GPIO | Connect | Notes |
|---|---|---|
| 25 | IBT-2 RPWM | forward PWM |
| 26 | IBT-2 LPWM | reverse PWM |
| 27 | IBT-2 R_EN and L_EN, joined | with the external 10 kΩ pull-down to GND |
| 36 | 12 V supply through the 100 kΩ / 22 kΩ divider | see below |
| 39 | the stand-in for the Hall sensor | see the two options below |

The full pin list is in [Wiring](wiring.md#pin-table).

**The supply must be readable.** Every move is refused while the controller can't read the 12 V supply or reads it below `roof.minSupplyVolts` (default 11 V). On the dashboard, `roof.supplyVolts` in `GET /api/status` is empty when the supply can't be read, and `roof.supplyLow` is true when it is unreadable or low. Feed the divider from the motor supply, with the motor supply on, and check the Supply reading on the Roof card before trying a move. A bench supply that sags under 11 V needs a lower minimum, within 6 to 15 V.

**A floating Hall pin is a fault.** Left unconnected, GPIO39 can read at either end of its range (30 counts or fewer, or 4065 or more: `HALL_RAIL_LOW_COUNTS` and `HALL_RAIL_HIGH_COUNTS`). Three such readings among the last eight latch a `hallRail` fault, and until the reading settles every move is refused (`HallNotSettled`). So GPIO39 always needs one of the two stand-ins below.

## Which firmware

Flash the standard `esp32dev` build over USB, not `esp32dev_bench`. The bench build simulates the roof and never switches the driver on, so it would never move the motor. From a checkout (the commands the [bench-mode](../user-guide/bench-mode.md#build-and-flash-usb-only) and [workflow](../development/workflow.md) pages use):

```bash
~/.platformio/penv/bin/pio run -e esp32dev -t upload     # the firmware
~/.platformio/penv/bin/pio run -e esp32dev -t uploadfs   # the web UI (filesystem image)
```

The release's [USB package](../getting-started/usb-flash.md) with esptool flashes the same standard firmware, if you'd rather not build. A new board then starts the RoofRoof-Setup hotspot: follow [First Boot](../getting-started/first-setup.md) to put it on your WiFi.

## Stand-in for the Hall sensor

The controller reads GPIO39 as raw counts (0 to 4095, the median of five readings) and classifies them. With the defaults (`roof.hallHighEnd` is `open`):

| Reading | Counts (default thresholds) | Meaning |
|---|---|---|
| High | 3000 or more | the open end's magnet |
| Between | in between | between the magnets |
| Low | 1100 or fewer | the closed end's magnet |

Leaving a magnet needs the reading to pass its threshold by a further 40 counts, and a change must be seen three times in a row to count. Voltage is `counts × 3.3 / 4095`: 3000 counts is about 2.42 V and 1100 counts about 0.89 V. The ESP32's ADC is not linear (it bends near both ends), so treat the voltages as approximate and **read the live value**: the **Hall reading** on the dashboard's Roof card, and the Hall scale under it, which draws your two thresholds. Turn the pot while watching it.

### Option A: a potentiometer

Wire 3.3 V, then about 1 kΩ, then a 10 kΩ potentiometer, then about 1 kΩ, then GND, with the wiper to GPIO39. The two series resistors keep the wiper away from the rails, so it reads roughly 340 to 3750 counts (0.28 to 3.0 V), inside the 30 to 4065 window that doesn't count as a fault. Use the 3.3 V pin, never 5 V or 12 V.

- Wiper toward 3.3 V (above 3000 counts): the **open** magnet.
- Wiper toward GND (below 1100 counts): the **closed** magnet.
- In between: between the magnets.

To make a move **arrive**, turn the pot to that end while the motor runs. From a magnet, turn the pot off it within `roof.leaveMagnetSeconds` (default 10 s) of starting, or the move latches `didNotLeave`. Reaching the opposite end's magnet is the target; reaching the start magnet again after leaving it latches `wrongMagnet`.

### Option B: a fixed mid-point

Two 10 kΩ resistors from 3.3 V to GND with the junction on GPIO39 read about 2048 counts (1.65 V): always between the magnets, never an end. The position is therefore always unknown, which changes what works:

- A person's **Open** and **Close** from the web UI still run: from an unknown position the controller allows them for a person (`Origin::Person` in `requestMove`), and moves slowly at `roof.slowDutyPercent`. Alpaca, MQTT and the automatic close are refused (`PositionUnknown`).
- Each move can only end at its **timeout**, which latches a `timeout` fault: the motor stops and the roof state is Error, and every motion is locked out until you press **Clear fault** on the Roof card. A fault never clears itself, and a restart doesn't clear it either. Clearing it leaves the position unknown, so the next move is slow again. (`driveBudget` can also latch after a few timed-out moves toward one end; Clear fault resets it too.)
- The automatic close refuses from an unknown position and sends the critical **Roof can't close by itself** alert, so Option B can show you that alert but never a real close.

## Bench settings

Use short, cautious settings for the first runs. They are in **Settings → Roof**; the ranges are in `test/fixtures/config-ranges.json`:

| Setting | Bench value | Range |
|---|---|---|
| `roof.travelMm` | 200 | 100 to 20000 |
| `roof.speedMmPerSecond` | 20 | 1 to 500 |
| `roof.topDutyPercent` | 40 | 20 to 100 |
| `roof.timeoutMarginSeconds` | 10 | 3 to 300 |
| `roof.leaveMagnetSeconds` | 10 (default) | 2 to 120 |

The hard timeout is the time the move should take plus the margin: about `travelMm / (speedMmPerSecond × duty)`, with the ramps counted, plus `roof.timeoutMarginSeconds`. From an unknown position the controller uses the whole travel at `roof.slowDutyPercent` (default 40, never above the top duty) instead, so with these numbers it is 200 / (20 × 0.4) = 25 s plus 10 s = 35 s. Saving settings while at an end can drop the position to unknown, as the estimate was made with the old values.

## Weather and park

**Alerts first.** In **Settings → Alerts** turn on **Send alerts**, set up a channel and use **Send test** ([Alerts](../user-guide/alerts.md)). The critical alerts below go nowhere otherwise.

**Weather.** In **Settings → Roof → Weather sources** switch on the HTTP source and set `safety.http.url` to `http://<sqmeter>/api/safety` ([Safety inputs](../user-guide/safety-inputs.md)). Opening is refused unless the verdict is safe, so wait for `Weather safe` first. To make it unsafe, use SQMeter's manual override, or unplug SQMeter: within the stale time (`safety.staleSeconds`, default 30 s) the verdict turns unsafe.

**Park.** The close on unsafe weather closes the roof only onto a mount that N.I.N.A. confirms is parked. To test that, run N.I.N.A. with the Advanced API plugin and a simulator mount, and set `park.nina.url` to `http://<pc>:1888`. Leave `park.switches` at `none`, as there are no switches.

Without N.I.N.A. the park is never confirmed. With `park.nina.url` empty it fails at once, and with a URL that nothing answers it fails after `park.timeoutSeconds` (default 240 s). Either way a critical **mount didn't park** alert is sent and the roof stays open, by design: the roof never closes onto a mount that isn't confirmed parked.

!!! warning "Unsafe weather with the roof open starts the automatic close"
    With the roof not closed, the mount parked and the verdict unsafe, the controller commands a close. With Option A and the pot at the closed end this does nothing. With the pot elsewhere, or Option B, the close runs to its timeout and latches a fault. Keep your hand near the 12 V switch.

## A first run

1. Pot in the middle (or Option B), weather safe, supply reading about 12 V, no fault.
2. Press **Open**. The motor runs slowly. With Option A turn the pot to the high end and the roof reads Open; with Option B wait for the timeout fault, then **Clear fault**.
3. With Option A, press **Close**, turn the pot off the high end within 10 s and then to the low end. Press **Stop** once mid-move to see the motor stop.
4. Try the weather and park cases above.

## The calibration run

The [calibration run](../user-guide/calibration-run.md) needs real magnets, or the potentiometer of Option A: each leg ends only at a magnet reading, and the run needs both ends and the level between them. With Option B (a fixed mid-point) every leg runs to its time limit and faults `timeout`. With the pot, start a run with the pot at the low end and **Closed**, then turn it through the middle to the high end once the motor runs, and back for the close; set `roof.calibrationLimitSeconds` to `30` first so a slow hand faults quickly. The times and the proposed speed then only measure your hand.
