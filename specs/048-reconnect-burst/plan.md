# Implementation Plan: A Gradual Network Return

**Branch**: `spec/048-reconnect-burst` | **Spec**: [spec.md](spec.md)

## Summary

Record each network return (`ConnectionBudget::NetworkReturn`) and give the minute after it a
schedule: inbound connections capped at accept with a fixed 503 (`InboundGate`), one built reply at
a time, MQTT 10 s later with backoff and a memory rule (`MqttRetry`, `backgroundFits`), discovery
paced one config per 200 ms and skipped when the broker kept it (`HaDiscovery::Pacer`, the retained
marker), queued alerts from 15 s one per 5 s. The census marks the returns in its trend and lists
each with the heap's lows after it, sampled ten times a second. A soak cuts the network N times.

## Constitution check

- **I/II/III (safety)**: no change to motion, close, park or weather; the safety poller is not
  touched (spec 047 owns it) and its polls are never delayed or capped. A person's command from the
  web page can be answered "busy" once in the minute after a return (owner assumption); the roof's
  own stops don't depend on the network.
- **IV (Alpaca)**: Alpaca's answers are unchanged; in the minute after a return a connection may be
  answered 503 before its request is read (as at under 2 KB since spec 037). ConformU runs against
  the simulator, unaffected.
- **V**: `NetworkReturn`, `inbound()`, `repliesAllowed()`, `MqttRetry`, `backgroundFits()`, the
  alert pacing, `describe()`, `HaDiscovery::Pacer`, `publishOne`, the marker: pure, native tests
  (`test_network_return`, `test_discovery_pacer`, the model `test_return_burst`); the glue
  (`InboundGate`, `NetState`, `HeapMap`, `MQTTClient`, `WebServer` discovery, `AlertOutbox`) is
  verified on the board by the soak.
- **VI (budgets)**: about 0.4 KB static data; no new task, stack or kept buffer; flash within budget.
- **VIII**: `GET /api/diag/heap` keeps its password; the marker topic carries a hash, no secret.
- **IX (docs)**: REST (`/api/diag/heap`), MQTT reference and user guide (discovery pacing, the
  marker, reconnect timing), heap budget page, troubleshooting, changelog, roadmap, OpenAPI.

## Design

| Piece | Where |
|---|---|
| returns, phases, pacing rules, record lines | `lib/ConnectionBudget/include/NetworkReturn.h`, `src/NetworkReturn.cpp` |
| one config at a time, the marker | `lib/HaDiscovery` (`count`, `publishOne`, `markerOf`, `Pacer`) |
| reply cap with a limit | `HeapSafe::replySlotFree(path, isPost, inFlight, limit)` |
| the gate at accept, the ticket | `include/InboundGate.h`, `src/InboundGate.cpp`, `Ipv6Network::listen` |
| feeding the returns | `NetState::tick/sinceReturn/phase/returns/note*/copyReturns` |
| MQTT | `MQTTClient::reconnect` (`MqttRetry`), `connect()` returns whether it tried |
| discovery | `WebServer::publishDiscovery/restartDiscovery/stepDiscovery/subscribeDiscoveryMarker` |
| alerts | `AlertOutbox::handle` |
| census | `HeapMap::upkeep` (10 Hz while followed), `writeTrend` marks, `writeReturns` |
| soak | `tools/soak/return_soak.py`, `test_return_soak.py` (CI) |
