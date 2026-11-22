# Quickstart: Bench Resilience

## Native tests

```bash
~/.platformio/penv/bin/pio test -e native -f test_park_grace -f test_park_logic -f test_roof_gates -f test_config_save -f test_calibration_run
```

## Web

```bash
cd web && npx vitest run configPatch ConfigConflict Settings Wizard Calibrate SqmeterCopy && npx playwright test
```

## Two tabs against the mock device

The Vitest suite `ConfigConflict` posts from two editors loaded at the same revision: different
fields both save; the same field gets 409, and Reload keeps the other edits.

## Bench (pending, real board and roof)

- During a close, block N.I.N.A.'s port (or pull its network) for 3 s: the close completes, and the
  log shows the ride-through start and end.
- During a close, kill N.I.N.A.'s network: the roof stops no later than 10 s after the last good
  reading (log timestamps), with "Park lost during the close: N.I.N.A. not read within its
  freshness limit".
- During a ride-through, open a fitted park switch: the roof stops at once.
- During a close, unpark the mount in N.I.N.A.: the roof stops at the next poll, as before.
- Two browser tabs on Settings: change the N.I.N.A. address in one and IPv6 in the other (loaded
  first); both changes stay. Change the N.I.N.A. address in both: the second save is refused naming
  it; Reload keeps its other edits.
- Wi-Fi setup in one tab, a Wi-Fi password typed in an older tab: the older save is refused naming
  `wifi.ssid`; the device stays on the new network.
