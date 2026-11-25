# Quickstart: spec 027

1. `~/.platformio/penv/bin/pio test -e native -f test_roof_gates -f test_park_wait -f test_roof_controller`
2. `cd web && npx vitest run src/lib/__tests__/moveBlockers.test.ts src/dashboard`
3. Demo (`npm run dev` in `web`): unpark the mount, press Close: "Waiting for the mount to park", then
   closed. Sun up and weather unsafe: Open lists 2 reasons and **Open anyway** opens it.
4. Bench: N.I.N.A.'s dome sequence with the mount unparked; Settings → Roof → N.I.N.A. **Test**.
