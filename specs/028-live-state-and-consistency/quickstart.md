# Quickstart: checking spec 028

1. `~/.platformio/penv/bin/pio test -e native -f test_connection_budget -f test_diag_store`
2. `cd web && npm run typecheck && npm test && npm run build:demo && npx playwright test`
3. In the demo: open the dashboard and hover the Status card's "?": the tip is over the Roof card.
4. In the demo with the browser's network throttled to "Slow 3G": the dashboard says "Connecting"
   until the first reading; turn the network off and on: "Reconnecting", then live again.
5. Start a move in the demo: the dashboard's position and the diagnostics page both update twice a
   second.
6. Size: `python3 tools/ui/pack_data.py --dist web/dist --out data && python3 tools/ui/size_check.py --data data`.
