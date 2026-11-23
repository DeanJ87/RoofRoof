# Contract: Test box mode

## Settings (`GET` / `POST /api/config`)

```json
"testBox": { "park": false, "weather": false, "daylight": false, "hours": 72, "until": 0 }
```

- `until` is read-only: a value in a save body is ignored; the device sets it (data-model.md).
- A save is refused (400, `error`) when:
  - `testBox.park` would be on with `park.nina.url` set: "Test box: the park bypass is for a box
    with no telescope; clear the N.I.N.A. address first (or turn the bypass off)";
  - `testBox.weather` would be on with `safety.http.enabled` or `safety.mqtt.enabled`: "Test box:
    the weather bypass is for a box with no weather source; turn SQMeter HTTP and MQTT off first
    (or turn the bypass off)";
  - `testBox.hours` is outside 1-720;
  - a bypass turns on (or `hours` is sent with one on) while the clock is not set: "Test box: the
    clock isn't set yet (Settings → Time); a bypass needs a deadline".
- Renew: `POST /api/config {"testBox": {"hours": N}}` with a bypass on.

## Status (`/api/status`, `/ws/status`: `roof.testBox`; MQTT `<base>/state`: `roof.testBox`)

```json
"testBox": {
  "set": true,
  "active": true,
  "park": true,
  "weather": true,
  "daylight": false,
  "until": 1764160000,
  "expired": false,
  "waitingForClock": false,
  "text": "Test box: park check (no telescope), weather check (no weather source) bypassed until 2026-11-26 12:26 UTC"
}
```

- Always present. With nothing set: every boolean false, `until` 0, `text` "".
- `park`, `weather`, `daylight`: in effect now. `set`: any set in the settings.
- `text` is "" when nothing is set; "Test box bypasses expired at <time> UTC: the checks are back"
  when set and expired; "Test box bypasses wait for the clock: the checks apply until it is set"
  while waiting for the clock.
- The park and weather texts (`roof.parkText`, `roof.weatherText`) end in "bypassed (test box): no
  telescope under this roof" and "bypassed (test box): no weather source" while those are in effect;
  `parkConfirmed` is true and `weather` is `safe`.

## Alpaca

The Dome's `description` (management API and `GET .../description`) starts with
`TEST BOX (bypassed: park check, weather check, daylight close): ` naming those in effect.

## Home Assistant

A `binary_sensor` `<id>_test_box`, device class `problem`, state `value_json.roof.testBox.active`,
attributes the `roof.testBox` object, from the `<base>/state` topic.

## Alert

Event `test_box` ("Test box mode on"): "Checks bypassed for a test box: {reason}. Faults still stop
the roof." `{reason}` is the status text.
