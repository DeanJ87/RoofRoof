# Quickstart: Setup Wizard Journey

## Run it

```bash
cd web && npx vite --config vite.demo.config.ts
```

Open the demo, **Demo → Restart setup wizard**.

## Scenarios

1. **Real observatory**: name → board (DevKit) → Check wiring → "Yes, or there will be" →
   SQMeter: Copy from SQMeter, apply → (no site screen: location and time follow SQMeter) →
   daylight → park switches → N.I.N.A. Test → direction: tick "I am at the roof", hold Open,
   "Yes, it opened" → teach: mark closed, hold Open to the end, mark part-way on the way and open,
   Confirm and save → calibration: tick, travel, Start, "Yes, it is open", Accept and save →
   alerts: tick ntfy and Pushover, fill the keys → password → review → Finish. The address stays
   `#/wizard` throughout.
2. **Test box**: restart; "No telescope yet (bench setup)" → SQMeter screen says the weather check
   is bypassed → no N.I.N.A. screen → the motor screens name the bypassed checks.
3. **Resume**: at any part, reload: the same part opens. Choose "No password", continue, reload,
   Back to the password screen: "No password" still chosen, Continue available.
4. **Re-run**: System → Run setup wizard; continue to part 4; reload: part 4 opens.
5. **Alerts validation**: tick Pushover, leave its keys empty, Continue: the errors sit under the
   two fields and the note names them.

## Automated

```bash
cd web && npx vitest run src/components/wizard src/components/__tests__/Wizard*
npm run build:demo && npx playwright test tests/wizard.spec.ts tests/wizard-a11y.spec.ts
```
