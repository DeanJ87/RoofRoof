# Quickstart: Test Box Bypass

## Native tests

```bash
~/.platformio/penv/bin/pio test -e native -f test_test_box -f test_test_box_surfaces -f test_daylight -f test_roof_alert_events \
  -f test_ha_discovery -f test_roof_status -f test_alpaca_description -f test_config_model -f test_roof_gates
```

## Web

```bash
cd web && npx vitest run TestBox Wizard review configSchema settingsDeps && npx playwright test test-box
```

## The demo

1. `cd web && npm run dev`, open the demo, Settings → Roof → Test box.
2. Turn on "No telescope under this roof" and "No weather station": the red banner shows on every
   page; the dashboard's park and weather rows say "Test box bypass"; Open is accepted.
3. Turn them off: the banner goes, Open is refused with "No weather source".

## Bench (pending, real board)

- Fresh board, no N.I.N.A., no SQMeter: turn the three bypasses on; jog, teach, a calibration run,
  Open and Close all work; the log has "Test box mode" warnings at the save and at the next boot.
- With the bypasses on, unplug the Hall sensor (rail): the move stops with the Hall fault and
  latches as before. Drop the 12 V supply below the minimum: refused/latched as before.
- With a park switch fitted and open: Close is refused.
- An alert channel set up: "Test box mode on" arrives at the save and after a restart.
- Home Assistant: the "Test box" problem sensor is on with its attributes.
- N.I.N.A.'s Dome description starts with "TEST BOX".
- Set hours to 1 and wait: after the hour the banner says expired and Open is refused again.
