# Quickstart: Advanced Diagnostics

## Automated

```bash
~/.platformio/penv/bin/pio test -e native -f test_diag_recorder -f test_diag_documents -f test_diag_store -f test_config_diag
~/.platformio/penv/bin/pio run -e esp32dev
bash tools/demo-core/build.sh
cd web && npm run typecheck && npx vitest run && npx playwright test tests/diagnostics.spec.ts
```

Then the whole of `specs/001-platform-bootstrap/quickstart.md` §1.

## In the demo

1. Settings → Device → Diagnostics: switch on "Advanced diagnostics", save.
2. System → Diagnostics → Open the page. Press Open on the Stop bar's page (the dashboard) or the
   Roof card; on the Diagnostics page the tiles change four times a second and the chart grows.
3. After the move: the move list shows it first with `arrived`; open it, download the CSV (one row
   per point).
4. Demo panel → Jam, then Close: the move ends in a fault with its measured value and limit.
5. Switch the mode off: the System card disappears; the page says the mode is off; `/api/diag`
   answers `{"enabled": false}`.

## On the board (bench check)

1. Bench firmware, mode off: note `maxStepGapMs` from `/api/status` after ten moves.
2. Mode on: ten moves; on `/api/diag` check `control.maxStepUs` is well under 20,000 (expect under
   2,000), `maxGapMs` is as before, `watchdogMarginMs` ≥ 70, and `loop.maxPassMs` is as before
   with the page open and streaming.
3. `memory.minFreeHeap` stays above 40 KB with the page open and an alert test sent (the TLS
   session's need); `stackFree.control` and `stackFree.loop` stay above 2 KB.
4. With "Keep move summaries" on: after a move, wait 5 minutes, restart; the list shows the
   summaries as kept.
5. With "Diagnostics over MQTT" on: `mosquitto_sub -t '<base>/diag/#' -v` shows nothing during a
   move, the summary after it, and the live document once a minute at rest.
