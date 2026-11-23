# Quickstart: Park Source and SQMeter Secrets

1. Demo: Settings → Roof → Mount park: choose "Park switches only", two switches; Close works with
   both closed, and the status names the source.
2. Demo: open the roof three times without moving the mount (switch watch): the fourth Open is
   refused with the stuck-switch text; Demo panel "park fails" (switch reads open) clears it.
3. Demo: Settings → Network → Copy from SQMeter with the demo SQMeter locked (login admin): rows
   for MQTT on and the password received; save; MQTT is on and `GET /api/config` shows the
   password masked.
4. Native: `pio test -e native -f test_park_source -f test_sqmeter_secrets`.
