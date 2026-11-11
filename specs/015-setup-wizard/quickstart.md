# Quickstart: Setup Wizard

## Native tests

```bash
~/.platformio/penv/bin/pio test -e native -f test_inputs_report -f test_config_setup -f test_config_migration -f test_config_model
```

## Web

```bash
cd web && npx vitest run wizard setup && npx playwright test tests/wizard.spec.ts
```

## Demo

`cd web && npm run build:demo && npm run preview:demo` (or the demo artifact from CI):

1. Demo panel → "Restart setup wizard": the wizard opens at page 1.
2. Go through the pages with "Save and continue". On Weather press Test: "Safe", the SQMeter source
   good. Turn on "Unsafe" in the panel and Test again: "Unsafe" with the reason. On N.I.N.A. press
   Test: connected, at park.
3. Open the roof from the dashboard and go back to the wizard: the Stop bar shows and a save is
   refused while the roof moves.
4. The review lists every check; Finish returns to the dashboard with no banner. System → Setup →
   "Run setup wizard" opens page 1 again.

## Bench (pending, real board)

- A factory-reset board, after joining Wi-Fi, opens the wizard on its first dashboard visit.
- An upgraded board shows the banner, not the wizard; dismissing it sticks across a restart.
- Page 3 shows the real supply volts within 0.2 V of a meter, and the park switches and rain
  contact change state live when operated by hand.
- Page 4's scale follows a hand-held magnet.
- Test on the weather and N.I.N.A. pages against the real SQMeter and N.I.N.A.: results within a
  few seconds; a wrong address shows unreachable; the main loop's timing (`maxStepGapMs`,
  `stackFree.loop`) unchanged while Test polls.
