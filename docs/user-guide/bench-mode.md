---
description: "Bench mode: a separate RoofRoof firmware for a bare ESP32 on the desk. The roof is simulated and the motor driver is never switched on, so the web UI, MQTT and the Alpaca Dome (ConformU included) can be tried without a roof."
---

# Bench mode

Bench mode is a separate build of the firmware for a **bare ESP32 on the desk**. The roof is
simulated: the firmware's own motion logic drives a simulated motor, magnets and Hall sensor
instead of the IBT-2, so the dashboard, the REST and MQTT commands and the ASCOM Alpaca Dome
behave as they would on the roof. The motor driver's pins are never switched on.

Use it to:

- try the web UI, Home Assistant and N.I.N.A. against the real board before the roof exists;
- run ASCOM ConformU against the real device (the constitution allows ConformU on real hardware
  only in bench mode or with a person at the roof);
- check a firmware change's motion and safety behaviour on the bench before it runs on the roof.

!!! danger "Never on the roof"
    A bench board reports a roof that isn't there. On the roof, it would tell N.I.N.A. the roof had
    closed while the real one stood open. Bench firmware is for a board with **nothing connected**.
    The safeguards below make that hard to get wrong; don't work around them.

## Build and flash (USB only)

```bash
~/.platformio/penv/bin/pio run -e esp32dev_bench -t upload     # the firmware
~/.platformio/penv/bin/pio run -e esp32dev_bench -t uploadfs   # the web UI
```

The bench image isn't published with the releases. Build it from a checkout.

!!! warning "Tie GPIO36 to ground on a bare board"
    The bench firmware refuses to simulate while it sees a 12 V supply (see below). On a bare
    DevKit, GPIO36 (the supply sense input) floats and can read high. Fit the 100k/22k divider or
    tie GPIO36 to GND with a jumper or a 10k resistor.

To leave bench mode, install the standard firmware: over USB, or through **Updates** in the web UI
(a bench board accepts the standard image).

## What is real and what is simulated

| Real (the standard firmware's code) | Simulated |
|---|---|
| The motion logic: states, ramps, timeouts, the position estimate, the fault latch | The motor: the roof moves at the configured speed scaled by the duty |
| The control task, its watchdog and the step-gap fault | The Hall sensor: the configured thresholds and polarity, a magnet at each end |
| The weather and park gates, the close sequence, the alerts | The 12 V supply (12.6 V, sagging a little while moving) |
| REST, WebSocket, MQTT, Home Assistant, the Alpaca Dome | The motor current, when current sense is on |
| The fault store: a latched fault survives a restart | |
| SQMeter, N.I.N.A., the park switches and the rain contact, if you connect them | |

The simulated roof starts **half-way** after every restart, so it reads *unknown* (and the Dome
reports a shutter error) until you move it: open or close it from the web UI (a person's command
moves it slowly to the first magnet). A bench board that somehow ends up on a roof therefore never
starts by reporting a closed roof. Bench mode has no fault injection: try
jams, a low supply and unsafe weather in the [demo](../live-demo.md#the-roof).

## The safeguards

- **A separate build.** The standard firmware contains no bench code. No setting, command, backup
  restore, MQTT message or Alpaca call can switch a roof controller into bench mode.
- **The motor driver's pins stay low.** In the bench build, EN (GPIO27), RPWM (GPIO25) and LPWM
  (GPIO26) are set low at start-up and never written high, and PWM is never attached to them. The
  rule is one function with a native test over every command and duty.
- **The standard firmware refuses a bench image** over the network:
    - **Updates** in the web UI refuses it before anything is installed: *"This is a bench-mode
      image (simulated roof, no motor): install it over USB."*;
    - command-line OTA (`espota`, `pio run -t upload --upload-port <ip>`) checks the image it just
      received before it is booted, keeps the running firmware and erases the refused image's
      first sector, so it can't be booted later either. The upload tool still reports success; the
      device's log says it was refused.
- **A bench board on a powered roof stops itself.** The bench firmware still reads the real
  12 V supply input. If it reads 6.0 V or more (or can't be read) for a second, the roof latches
  the fault `benchSupply` (*"Bench firmware on a powered roof: supply 12.4 V >= 6.0 V"*): it reads
  Error, the Dome's shutter reports an error, and nothing moves, simulated or real. It checks for a
  second at start-up before anything can be commanded, then on every step. Clearing the fault
  while the supply is still there latches it again.
- **It says so everywhere:**
    - a banner on every page of the web UI;
    - `roof.simulation` is `bench` in [`/api/status`](../api/rest.md), `/ws/status` and the MQTT
      state document (`off` on the roof), and Home Assistant shows the firmware version with
      "(bench)";
    - the Alpaca Dome's description starts *BENCH MODE (simulated roof, no motor)*;
    - every alert's title starts *Bench:*;
    - the log says bench mode at start-up.

## Check it on your board

Before you rely on a bench board, measure GPIO27, GPIO25 and GPIO26 with a meter or a scope while
you open and close the simulated roof: all three stay at 0 V.
