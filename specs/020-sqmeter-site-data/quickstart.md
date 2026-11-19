# Quickstart: Site Data from SQMeter

## Automated

```bash
~/.platformio/penv/bin/pio test -e native -f test_sqmeter_site -f test_config_location -f test_config_migration
~/.platformio/penv/bin/pio run -e esp32dev
cd web && npm run typecheck && npm test -- --run && npm run build && npx playwright test
```

Expected: the parsing, schedule, save rule, migration and site document suites pass; the image
builds with zero warnings and stays within the flash budget; the Settings and wizard copy flows
pass in the demo.

## In the demo

1. Open the demo, Settings → Roof. The location card says "From SQMeter", with the simulated GPS
   fix and "read just now".
2. Demo panel → "SQMeter offline". The dashboard's daylight row says SQMeter isn't answering and
   keeps the location; the weather source goes stale (unsafe), as before.
3. Settings → Roof → "Copy from SQMeter": the list shows the time zone, NTP, MQTT broker (with
   "enter the MQTT password"), weather topic and ntfy rows. Tick two, Apply, Save.
4. Demo panel → restart the wizard. On Site and time, the SQMeter screen comes first; Copy, Apply,
   Continue: the Time screen shows SQMeter's time zone.

## On the bench (pending; see the PR's checklist)

- A real SQMeter with a typed location, then with a GPS fix: RoofRoof takes each, saves once, and
  shows the source and age.
- SQMeter unplugged: the kept location stays, daylight carries on, the status says not answering;
  the poller's stack low-water and the N.I.N.A. reading's age are unchanged.
- Copy from a real SQMeter: the list matches SQMeter's settings; no secret appears in the reply
  (check with `curl -u ... -X POST /api/sqmeter/site`).
