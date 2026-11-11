# Quickstart: Alpaca Dome

## 1. Local checks

Everything in [spec 001's quickstart §1](../001-platform-bootstrap/quickstart.md) passes. The new
native suites are `test_alpaca_dome` and `test_dome_bridge`.

## 2. The simulator and ConformU

```sh
tools/alpaca-sim/build.sh
./alpaca-sim 11111 &              # closed, weather safe, mount parked
conformu conformance http://127.0.0.1:11111/api/v1/dome/0
conformu alpacaprotocol http://127.0.0.1:11111/api/v1/dome/0
```

Expected: both exit 0 with no `ERROR`, `ISSUE` or configuration alert lines.

## 3. Refusals against the simulator

```sh
./alpaca-sim 11112 unsafe &
curl -s -X PUT -d 'Connected=true' http://127.0.0.1:11112/api/v1/dome/0/connected
curl -s -X PUT http://127.0.0.1:11112/api/v1/dome/0/openshutter
```

Expected: `ErrorNumber` 1035 (0x40B) and `ErrorMessage` starting `Weather not safe:`. With
`unparked`, an open succeeds and a close answers `Mount not parked: ...`.

## 4. Bench (motor disconnected)

1. Turn on Alpaca, restart, add the Dome in N.I.N.A. (Equipment → Dome → ASCOM Alpaca discovery).
2. Weather safe: Open; the dashboard and N.I.N.A. show opening. Stop from N.I.N.A.'s dome panel stops it.
3. Unsafe: Open shows N.I.N.A.'s error with the reason.
4. Mount not parked: Close shows the park reason.
5. With the automatic close running, Stop from N.I.N.A. stands it down and the critical alert goes out.
