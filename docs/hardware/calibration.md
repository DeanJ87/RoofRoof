---
description: "Calibrating the roof controller: the Hall sensor thresholds and polarity from the live reading, the 12 V supply reading, and the current-sense scale."
---

# Calibration

The roof's real numbers are settings, measured once the roof is built: the Hall thresholds and which end reads high, the travel and speed, and (if fitted) the current-sense scale. This page covers the readings the controller takes from the board. The live values are in the `roof` section of [`GET /api/status`](../api/rest.md#get-apistatus); settings are changed with [`POST /api/config`](../api/rest.md#post-apiconfig) (the settings page gets a Roof tab in a later release).

!!! danger "Motor disconnected"
    Calibrate with the motor's leads disconnected from the IBT-2 (or the motor supply off), until the Hall thresholds are set and checked. A wrong threshold can make the controller see a magnet that isn't there.

Read the roof section with:

```bash
curl -s http://roofroof.local/api/status | python3 -c 'import json,sys; print(json.dumps(json.load(sys.stdin)["roof"], indent=2))'
```

---

## Hall sensor

The controller reads the SS49E on GPIO39 as raw 12-bit counts (0-4095), the median of five readings every 20 ms: `roof.hall.counts`. `roof.hall.level` is how it classifies that reading against the thresholds: `low`, `between`, `high`, `rail` (a disconnected or shorted sensor) or `unsettled` (not three equal classifications in a row yet).

The worm gear holds the roof wherever it stops, so with the motor disconnected the roof doesn't move by hand. Take the end readings by bringing the sensor to each magnet instead: on a sensor that isn't fixed yet, hold it where it will sit at each end; on a fixed sensor, unbolt each magnet in turn and hold it at its stopping position in front of the sensor.

1. **Between the magnets.** With no magnet near the sensor, note `roof.hall.counts`: the resting value, typically about 2000 with a 10k/20k divider.
2. **At each end.** Hold the closed end's magnet where it sits when the roof is closed, and note the counts; then the open end's. One reads well above the resting value and the other well below (the magnets have opposite poles). Repeat a few millimetres short of the stopping position: the roof must still read past the threshold there.
3. **Which end is high.** Set `roof.hallHighEnd` to the end that read high (`open` or `closed`).
4. **Thresholds.** Set `roof.hallHighCounts` about halfway between the resting value and the high end's reading, and `roof.hallLowCounts` about halfway between the resting value and the low end's reading. They must be at least 80 counts apart and inside 100-3995. Leave a margin: leaving a magnet needs the reading 40 counts back past its threshold.
5. **Check.** At each end `roof.state` is that end's (`closed` or `open`), and between them `unknown` (or `stopped` after a stopped move).

```bash
curl -X POST http://roofroof.local/api/config -H "Content-Type: application/json" \
  -d '{"roof": {"hallHighEnd": "open", "hallHighCounts": 2900, "hallLowCounts": 1150}}'
```

A reading at 30 counts or below, or 4065 or above, is a rail: three of them among the last eight latch a `hallRail` fault. With the sensor's divider fitted, a disconnected sensor output reads 0.

## Supply

`roof.supplyVolts` is the 12 V supply measured on GPIO36 through the 100k/22k divider, with the ESP32's factory ADC calibration. Measure the supply at the IBT-2's input with a meter and compare: within about 0.2 V is normal. `roof.supplyLow` is true when the supply is unreadable or below `roof.minSupplyVolts` (open and close are refused; during a move it is a fault). A reading of 0 V usually means the divider isn't connected.

## Current sense

Optional, and off by default (`roof.currentSenseEnabled`). The IBT-2's R_IS and L_IS outputs go through 10k/20k dividers to GPIO34 and GPIO35. The controller reads them only with current sense on, takes the larger of the two, undoes the divider (× 30 / 20) and multiplies by `roof.currentAmpsPerVolt`: the motor amps per volt at the IS output. The default, 8.5 A/V, is the BTS7960's typical sense ratio (about 8500) into a 1 kΩ sense resistor; boards and chips vary.

The firmware divides the reading by the PWM duty that was applied (the sense output only follows the motor while its side is on), so compare at full duty first. Once the motor has run at 25% duty or more for a second, a reading under 0.2 A counts as a broken sense wire (`currentUnreadable`).

1. With current sense on and the motor disconnected, `roof.motorAmps` should read near 0.
2. Connect the motor with a clamp meter (or a meter in series) on one motor lead, open the roof under supervision, and compare `roof.motorAmps` with the meter during steady running.
3. Set `roof.currentAmpsPerVolt` to its current value × (meter amps ÷ `roof.motorAmps`).
4. Set `roof.maxCurrentAmps` above the steady running current and the start-up surge (which lasts well under the 250 ms the controller allows), and below the stall current.

## Travel and speed

Measure the travel between the two magnets' trigger points and set `roof.travelMm`; time a full move at 100% duty and set `roof.speedMmPerSecond` (travel ÷ seconds). The [Roof motion](../user-guide/roof-motion.md#settings) page explains how the timeouts and the slow-down come from them.
