# Quickstart: Jog and Teach the Ends

## Native tests

```bash
~/.platformio/penv/bin/pio test -e native -f test_roof_jog -f test_calibration_teach -f test_calibration_proposal -f test_calibration_run -f test_roof_gates -f test_roof_check
```

## Web

```bash
cd web && npx vitest run teach calibrat wizard && npx playwright test tests/calibrate.spec.ts
```

## Demo

`cd web && npm run build:demo && npm run preview:demo`, then open the calibration page:

1. Tick "I am at the roof and can see it". The roof starts closed: press "This is closed".
2. Hold "Open-direction" for a few seconds and release: the roof moves only while held. Press
   "This is part-way".
3. Hold "Open-direction" until it stops at the open magnet; press "This is open".
4. The proposal shows the high end and both thresholds beside the saved values; Confirm. Settings
   → Roof shows them, and the roof reads Open.
5. Turn on "Unsafe" in the panel: both hold buttons are refused with the weather's reason.

## Bench (pending, real board and roof)

- The jog stops on release, from a phone and from a desktop browser.
- The jog stops within half a second when the browser tab is killed and when the phone's Wi-Fi is
  switched off mid-jog.
- The taught thresholds match the real magnets: the roof reads each end at rest at its stopping
  point, reads "between" a few centimetres away from each magnet, and the 016 calibration run then
  stops at both magnets.
- A jog held for 30 s stops with `timeout`.
- The park interlock: a fitted park switch opened mid-jog stops the roof with `parkInterlock`.
