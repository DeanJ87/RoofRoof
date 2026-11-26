# Tasks: A Gradual Network Return

- [x] T001 Research from the board: F51's census trend matched to the tests, the 1 Hz samples after each return (research R1)
- [x] T002 `NetworkReturn` (returns from the link and from requests after a silence, phases, record) + native tests
- [x] T003 `inbound()`, `repliesAllowed()`, `backgroundFits()`, `MqttRetry`, alert pacing + native tests
- [x] T004 Model of a return under both schedules (`test_return_burst`, research R3)
- [x] T005 `InboundGate`: the fixed 503 at accept, the ticket on the request; `Ipv6Network::listen`
- [x] T006 The guard's reply cap in the window
- [x] T007 MQTT reconnect through `MqttRetry` and the memory rule; noted in the return's record
- [x] T008 `HaDiscovery::count/publishOne/markerOf/Pacer` + native tests; paced discovery and the marker in `WebServer`
- [x] T009 Queued alerts from 15 s, one per 5 s for a minute
- [x] T010 Census: 10 Hz samples while followed, trend marks, "Network returns" section; REST docs, OpenAPI
- [x] T011 `return_soak.py` + tests in CI
- [x] T012 Docs: MQTT reference and guide, heap budget, troubleshooting, changelog, roadmap
- [ ] T013 Bench: `return_soak.py --cycles 5 --down 90 --up 120` with the harness polling; the census's return lines at the end
