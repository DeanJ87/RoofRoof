# Quickstart: Validating Safety Inputs

## 1. Everything CI runs

Run spec 001's [quickstart §1](../001-platform-bootstrap/quickstart.md) in full. New native suites:

```bash
~/.platformio/penv/bin/pio test -e native -f test_safety_input_logic -f test_park_logic -f test_nina
```

Expected: all pass; zero warnings; image under 75% of the slot.

## 2. Settings round trip (any board, no SQMeter needed)

1. `POST /api/config` with `{"safety":{"http":{"enabled":true,"url":"https://x"}}}`: rejected,
   "SQMeter safety URL must start with http://".
2. Set `safety.http` to SQMeter's `http://<sqmeter>/api/safety` and enabled; restart; the
   setting is still there.

## 3. Bench checks (real board; logged over serial at 115200)

| Step | Expected log (`Safety` tag) |
|---|---|
| Boot with HTTP source on and SQMeter up and safe | `Weather unknown` for at most one poll, then `Weather safe` |
| Make SQMeter unsafe (e.g. manual override) | `Weather unsafe: SQMeter says unsafe: reasonFlags 0x1` within 5 s |
| Unplug SQMeter | `Weather unsafe: SQMeter HTTP unreachable ...` within 35 s |
| MQTT source on, stop SQMeter's MQTT (last will) | `SQMeter MQTT offline` at once |
| No N.I.N.A. URL | `Park: not parked: N.I.N.A. not configured` |
| N.I.N.A. running, mount parked, no switches fitted | `Park: parked` |
| Close N.I.N.A. | `not parked: N.I.N.A. unreachable` within 4 s |
| `switches` = one (no 003 reading yet) | `not parked: park switch 1 unreadable` |
