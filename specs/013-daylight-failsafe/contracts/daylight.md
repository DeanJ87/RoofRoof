# Contract: Daylight Failsafe

## Settings (`GET/POST /api/config`)

```json
{
  "daylight": { "mode": "alert", "sunAltitudeDeg": 0 },
  "location": { "set": false, "latitude": 0, "longitude": 0 }
}
```

`mode` is `off`, `alert` or `close`. A save outside the ranges answers 400 with the range.

## Status (`GET /api/status`, `/ws/status`)

`roof.daylight`:

| Field | Type | |
|---|---|---|
| `mode` | `off` \| `alert` \| `close` | |
| `state` | `off` \| `unknown` \| `night` \| `day` | |
| `sunAltitudeDeg` | number \| null | null while off or unknown |
| `limitDeg` | number | the threshold |
| `text` | string | "Sun 3.2° ≥ 0.0°", "Location not set", "Clock not set", "Off" |
| `closes` | bool | daylight wants the roof closed now (mode close and day) |

## Refusal

Open commands (REST `POST /api/roof/open`, Alpaca `OpenShutter`, MQTT and Home Assistant) answer
the refusal reason `daylight` with the text `Daylight: Sun 3.2° ≥ 0.0°` while
`roof.daylight.closes` is true and the weather is safe. The weather refusal
(`weatherNotSafe`) takes precedence.

## Alert events

| Event | Title | Default level | Variables |
|---|---|---|---|
| `daylight_open` | Roof open in daylight | 3 | `reason`, `action` |
| `daylight_closed` | Roof closed after daylight | 2 | `reason` |
| `daylight_unknown` | Daylight check not working | 2 | `reason` |

Critical events raised during a daylight-only close keep their types (`mount_not_parked`,
`roof_not_closed`), with "It is daylight" wording.

## Demo

The demo panel's roof section has a "Sun up" switch: the daylight state is `day` at the threshold
plus 10°, whatever the location and clock, until it is switched off.
