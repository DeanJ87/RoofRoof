# Contract exposed: settings (`GET/POST /api/config`)

Two new sections in the existing settings document. Defaults shown; every new source is off.

```json
{
  "safety": {
    "http": { "enabled": false, "url": "" },
    "mqtt": { "enabled": false, "topic": "sqmeter" },
    "staleSeconds": 30,
    "rainContact": false
  },
  "park": {
    "nina": { "url": "", "api": "v2" },
    "switches": "none"
  }
}
```

| Path | Rule | Message on failure |
|---|---|---|
| `safety.http.url` | required when `safety.http.enabled`; `http://` URL | `SQMeter safety URL must start with http://` / host error |
| `safety.mqtt.topic` | required and a valid topic when `safety.mqtt.enabled` | `SQMeter MQTT topic: use letters, numbers, _ and -, with / between levels` |
| `safety.staleSeconds` | whole number 10-600 (`test/fixtures/config-ranges.json`) | `Safety: stale time must be between 10 and 600 seconds` |
| `park.nina.url` | empty, or an `http://` URL | `N.I.N.A. URL must start with http://` / host error |
| `park.nina.api` | `v2` | `N.I.N.A. API version must be v2` |
| `park.switches` | `none`, `one`, `two` | `Park switches must be none, one or two` |

Settings dependency D-40: `safety.mqtt.enabled` needs MQTT on (`mqtt-off`) and the broker
connected (`mqtt-disconnected`).

The schema `specs/001-platform-bootstrap/contracts/schemas/config.schema.json` is regenerated from
the demo, which runs the same ConfigModel.
