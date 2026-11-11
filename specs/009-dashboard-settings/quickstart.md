# Quickstart: Dashboard and Settings

## Check it

```bash
cd web
npm run typecheck && npm test -- roofView useRoofCommand RoofCard RoofStopBar glance Settings
npm run build:demo && npx playwright test tests/dashboard.spec.ts tests/settings-tabs.spec.ts tests/a11y.spec.ts tests/layout-overlap.spec.ts
cd ..
python3 tools/dashboard/check.py
python3 tools/settings-deps/check.py
node tools/i18n/check.mjs && node tools/i18n/literals.mjs
python3 tools/ui/size_check.py
```

Then the full suite in `specs/001-platform-bootstrap/quickstart.md` §1.

## See it

`npm run dev` (MSW mocks): the dashboard shows the Roof card for a closed, healthy roof; Settings →
Roof shows every roof, weather and park setting. The demo (`npm run build:demo && npm run
preview:demo`) shows the same; the moving and faulted states are spec 011's simulated roof, and are
covered by the component tests until then.

## On the device (bench)

1. With the motor disconnected, open the dashboard: the Roof card shows the state, the Hall reading
   and the thresholds.
2. Move a magnet past the sensor: the Hall marker follows, and the level changes at the thresholds.
3. Open and Close with the weather safe and the mount parked; press Stop mid-move; press Stop on
   Settings while moving (the Stop bar).
4. Pull the park switch: Close disables with the reason.
5. Raise a fault (pull the Hall sensor): the card and the Status card show it; Clear fault clears it
   once the sensor is back.
