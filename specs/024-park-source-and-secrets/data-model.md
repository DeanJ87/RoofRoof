# Data Model: Park Source and SQMeter Secrets

## Settings

| Key | Type | Default | Rule |
|---|---|---|---|
| `park.source` | `nina` \| `switches` \| `ninaAndSwitches` | `nina` (new device); from `park.switches` on upgrade | `nina` needs `park.switches` none; the others one or two; `testBox.park` not with `switches`; a motion setting (refused while the roof is busy) |
| `park.switches` | `none` \| `one` \| `two` | `none` | unchanged |

## Device-owned (not settings)

- **Switch watch** (NVS `roofroof/parkWatch`, 2 bytes): roof openings since each switch last read
  open, 0-255. `stuck` = the first fitted switch at 3 or more, only with `switches`.
- **Held secrets** (memory, 30 min): SQMeter's MQTT broker, port, user name and password; ntfy
  server and token; Pushover user key and app token; when read.
- **Secrets read** (poller, memory): problem or the values, `atMs`, `basedOnMs` (the settings read).

## Status additions

- `roof.parkSwitches.switchesOnly`: the switches are the only park source.
- `roof.parkSwitches.openings`: `[n1, n2]`; `roof.parkSwitches.check`: null or the refusal text.
- `sync.secrets`: null (no group needs them), `received`, or the problem name.

## Copy document additions (`POST /api/sqmeter/site`)

- `mqtt.enabled`, `ntfy.enabled`, `pushover.enabled` (SQMeter's switches).
- `secrets`: `{problem, http, mqttPassword, ntfyToken, pushover}`; each secret is `none`,
  `received` (differs from RoofRoof's saved one) or `same`.
