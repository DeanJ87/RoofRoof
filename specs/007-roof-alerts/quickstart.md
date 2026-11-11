# Quickstart: Validating Roof Alerts

## 1. Everything CI runs

Run spec 001's [quickstart §1](../001-platform-bootstrap/quickstart.md) in full. New and touched
native suites:

```bash
~/.platformio/penv/bin/pio test -e native -f test_roof_alerts -f test_roof_alert_events \
  -f test_roof_alert_settings -f test_alert_clients -f test_alert_variables -f test_settings_deps \
  -f "test_close_*"
```

Expected: all pass (spec 005's close suites unchanged); zero warnings; the flash budget passes.

## 2. Settings round trip (any board)

1. `POST /api/config` with `{"alerts":{"events":{"mount_not_parked":{"level":2}}}}`: refused with
   "the roof's critical alerts are urgent or wake me".
2. `{"alerts":{"events":{"move_fault":{"level":0}}}}`: accepted, and still there after a restart.
3. A config saved before this spec loads with the roof events at their defaults.

## 3. Bench checks (real board, motor disconnected, one alert channel set up)

| Step | Expected |
|---|---|
| Settings > Alerts | seven roof rows; the two critical ones offer Urgent and Wake me only and stay editable with alerts off |
| Test on each roof row | a sample arrives with example values for `{reason}` |
| Alerts off (master switch), make it unsafe with N.I.N.A. closed | "Mount didn't park" still arrives after `park.timeoutSeconds` |
| Alerts on, roof open at the magnet, make it unsafe | "Unsafe: closing" with the weather's reasons |
| Let the close finish (bench mode or the motor connected and attended) | "Roof closed after unsafe weather" |
| Disconnect the Hall sensor during a move | "Roof fault" with the Hall reading |
| Lower the bench supply below `roof.minSupplyVolts` for 30 s | "12 V supply low" with the reading and the minimum |
| Stop SQMeter for 30 s past its stale time | "Weather source lost" naming SQMeter and why |
| Reset the board with the fault still latched | no second "Roof fault"; after a power cut, one after the 60 s grace |
| Owner review of the Settings > Alerts page | wording and layout accepted |
