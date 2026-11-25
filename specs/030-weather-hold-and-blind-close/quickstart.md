# Quickstart: Spec 030

1. Run spec 001's quickstart §1 (firmware build, native tests, web checks, Playwright, quality gate,
   flash budget, docs build).
2. Native: `pio test -e native -f test_weather_hold -f test_roof_blind -f test_needs_person -f test_alert_queue`.
3. Demo: make SQMeter unsafe for under a minute: the roof card counts down and nothing moves; let it
   run past the hold: the automatic close starts. Turn on rain: it starts at once.
4. Bench (pending, see spec "Left for the owner").
