# Quickstart: Validating the Roof Hardware

## 1. The pure rules (SC-001)

```bash
~/.platformio/penv/bin/pio test -e native -f test_roof_io -f test_roof_gates -f test_roof_status
```

Expected: every test passes: scaling and medians, switch confirmation and chatter, the watchdog
boundary and wrap, the fault record (round trip, empty, damaged), the gates and interlocks, the
settings-save guard, every fault and refusal text, the status section.

## 2. Settings and contracts

```bash
~/.platformio/penv/bin/pio test -e native -f test_config_model -f test_config_ranges -f test_config_migration
(cd web && npx vitest run src/__tests__/configRanges.test.ts src/__tests__/configSchema.test.ts src/demo/__tests__/contracts.test.ts)
python3 tools/settings-deps/check.py && python3 tools/api/routes.py --check
```

Expected: `roof.currentAmpsPerVolt` accepted at 0.5 and 100, rejected just outside; an old config
loads with 8.5; the demo's status (with its `roof` section) matches the contract schema.

## 3. Everything CI runs (SC-005)

`specs/001-platform-bootstrap/quickstart.md` §1, unchanged.

## 4. On the bench (SC-002 to SC-004)

With the motor disconnected (a lamp or a meter on the IBT-2 outputs) and a weather source and
N.I.N.A. configured:

1. Boot between the magnets: `roof.state` is `unknown`, EN stays low.
2. Hold a magnet at the sensor: `roof.hall.level` follows; set the thresholds from
   `roof.hall.counts` (calibration page).
3. `POST /api/roof/open` with the verdict safe: EN rises after the dead time; `POST /api/roof/stop`
   drops it at once.
4. A debug build that blocks the task for 500 ms while driving: EN falls within 110 ms of the last
   step and `roof.fault.cause` is `watchdog` (SC-002).
5. Latch a fault, power-cycle 5 times: still latched; `POST /api/roof/clear-fault` clears it (SC-003).
6. Open a fitted park switch during a close: the motor stops with `parkInterlock`.
