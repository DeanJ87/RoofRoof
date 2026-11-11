# Quickstart: Validating the Roof Motion Logic

The logic has no hardware glue yet (spec 003), so everything here runs on a desktop.

## 1. The logic and its safety properties

```bash
~/.platformio/penv/bin/pio test -e native -f "test_roof_*"
```

Expected: every test passes (five suites). `test_roof_properties` runs 10,000 seeded random sequences of
commands, origins and readings against a simulated roof (SC-001) and fails with the seed and step
of any broken invariant.

## 2. The settings

```bash
~/.platformio/penv/bin/pio test -e native -f test_config_model -f test_config_ranges -f test_config_migration -f test_config_backup -f test_settings_deps
(cd web && npx vitest run src/__tests__/configRanges.test.ts src/__tests__/configSchema.test.ts)
python3 tools/settings-deps/check.py
```

Expected: the `roof` ranges are accepted at their bounds and rejected just outside on both sides
(SC-004); a config from an earlier release loads with the roof defaults and current sense off.

## 3. Everything CI runs (SC-005)

`specs/001-platform-bootstrap/quickstart.md` §1, unchanged.

## 4. On the bench (spec 003 onwards)

Not runnable in this spec: the logic isn't linked into the firmware yet. The PR lists what the
bench must confirm once spec 003 wires it up (speed proportional to duty, the slow duty actually
moving the roof, the Hall thresholds, the confirm times against real start-up current).
