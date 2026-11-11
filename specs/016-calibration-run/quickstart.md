# Quickstart: Calibration Run

## Native tests

```bash
~/.platformio/penv/bin/pio test -e native -f test_roof_calibrate_leg -f test_calibration_run -f test_calibration_proposal -f test_roof_check -f test_platform_hold -f test_config_model -f test_config_ranges
```

## Web

```bash
cd web && npx vitest run calibrat && npx playwright test tests/calibrate.spec.ts
```

## Demo

`cd web && npm run build:demo && npm run preview:demo`:

1. Settings → Roof → Calibrate → "Open the calibration page".
2. Tick "I am at the roof and can see it", leave "Closed", Start. The roof opens and closes at
   the slow duty; the page shows each leg against its limit, Stop always visible.
3. The result shows the proposal beside the saved values; Accept and save; Settings → Roof shows
   the values.
4. Start again and press Stop mid-leg: "Stopped", no proposal, settings unchanged.
5. Turn on "Unsafe" in the panel: Start is refused with the weather's reason.

## Bench (pending, real board and roof)

- A run on the real roof, attended, the mount parked: each leg stops at its magnet; the proposed
  speed matches a stopwatch within 2 %; the proposed polarity matches stage 3's reading.
- A magnet unbolted: the slow open faults `timeout` at its limit on the end stop, motor and driver
  cool enough after it.
- Stop from the page, from the Stop bar and from the 12 V switch mid-leg.
- A restart mid-leg: nothing moves after the boot; no run active.
- With current sense: the recorded running current matches a clamp meter.
