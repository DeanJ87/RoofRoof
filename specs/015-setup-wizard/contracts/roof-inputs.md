# Contract: `GET /api/roof/inputs`

Open (readings only, like `/api/status`). GET only; no body; no side effects; no outgoing
request. Answers `200` with the document below. JSON Schema:
`specs/001-platform-bootstrap/contracts/schemas/roof-inputs.schema.json` (generated from the demo).

```json
{
  "weather": {
    "verdict": "safe",
    "text": "safe",
    "http": {
      "enabled": true, "state": "good", "generation": 1, "ageMs": 2100, "limitMs": 30000,
      "safe": true, "flags": 0, "text": ""
    },
    "mqtt": {
      "enabled": false, "state": "off", "generation": 0, "ageMs": 0, "limitMs": 150000,
      "safe": false, "flags": 0, "text": ""
    },
    "rainContact": "notFitted"
  },
  "park": { "parked": true, "text": "parked" },
  "nina": {
    "state": "good", "generation": 1, "connected": true, "atPark": true, "slewing": false,
    "ageMs": 900, "text": ""
  }
}
```

| Field | Values |
|---|---|
| `weather.verdict` | `safe`, `unsafe`, `unknown` (`Safety::verdictName`) |
| `weather.text` | `Safety::describe(verdict)`, the same as `/api/status` `roof.weatherText` |
| `weather.http.state`, `weather.mqtt.state` | `off`, `waiting`, `good`, `stale`, `unreachable`, `unreadable`, `offline` |
| `…ageMs` | since the last good reply (since the source started, without one) |
| `…safe`, `…flags` | SQMeter's `safe` and `reasonFlags` from the last good reply |
| `…text` | `Safety::describeSource`: "" when off or good and safe |
| `weather.rainContact` | `notFitted`, `dry`, `raining`, `unreadable` |
| `park.text` | `Safety::describe(park)`, the same as `roof.parkText` |
| `nina.state` | `notConfigured`, `waiting`, `good`, `unreachable`, `unreadable`, `apiError` |
| `nina.connected`, `atPark`, `slewing` | meaningful only when `state` is `good` |
| `nina.ageMs` | since the reading (good or failed) was taken |
| `nina.text` | the failure text ("" when good) |
| `…generation` | goes up when that part's settings change (its record starts again) |

No URL, topic, credential or secret is included.
