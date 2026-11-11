# Quickstart: Daylight Failsafe

## Native tests

```bash
~/.platformio/penv/bin/pio test -e native -f test_daylight -f test_close_daylight -f "test_close_*" -f test_roof_gates -f test_roof_alert_events
```

`test_close_daylight` includes the randomised runs of SC-002 and SC-003.

## Demo

`cd web && npm run build:demo && npm run preview:demo` (or the demo artifact from CI):

1. Settings → Roof → Daylight: mode "Alert and close".
2. Open the roof (weather safe), then switch on the demo panel's "Sun up".
3. The Roof card's daylight line says day; the demo asks for the park, the roof closes, and the
   alerts list shows "Roof open in daylight" and "Roof closed after daylight".
4. Open is disabled with the daylight reason. Set the mode to alert: Open is allowed again.

## Bench (pending, real board)

- With the location set and NTP on, the Roof card's sun altitude matches an almanac within 0.5°.
- With "alert and close", bench mode and the sun up (or a threshold below the current altitude),
  the simulated roof parks and closes; Alpaca `OpenShutter` answers the daylight error.
- With no NTP for 10 minutes, "Daylight check not working: Clock not set" arrives.
