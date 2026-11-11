# Contract: Interfaces After the Bootstrap

The shapes are SQMeter's. The JSON Schemas live in `contracts/schemas/` (moved from SQMeter's
`specs/016-demo-device-emulation/contracts/schemas/`), and the OpenAPI document is generated from
them (`tools/api/openapi.py`).

## REST routes kept

The registry `tools/api/routes.json` holds exactly these. CI fails on any difference with
`src/WebServer*.cpp` or `docs/api/rest.md`.

| Route | Method | Auth |
|---|---|---|
| `$probe` (captive-portal probes) | GET | open |
| `/api/status` | GET | open |
| `/api/settings/effective` | GET | open |
| `/api/config` | GET | open (secrets masked) |
| `/api/config` | POST, PUT | required |
| `/api/restart` | POST | required |
| `/api/backup` | POST | required |
| `/api/backup/preview` | POST | required |
| `/api/backup/apply` | POST | required |
| `/api/wifi/scan` | GET | required |
| `/api/wifi/connect` | POST | required |
| `/api/mqtt/test` | POST | required |
| `/api/update` | POST (multipart) | required |
| `/api/update/fs` | POST (multipart) | required |
| `/api/alerts/test` | POST | required |
| `/api/alerts/clear` | POST | required |
| `/api/alerts/arm` | POST | required |
| `/api/alerts/disarm` | POST | required |
| `/api/alerts/armed` | GET | open |
| `/api/alerts/recent` | GET | open |
| `/setup` | GET | open |
| `/management` | GET | open |
| `/api/v1` | ANY | open (answers "unknown device" until spec 006) |

Each route's auth is unchanged from SQMeter's `tools/api/routes.json` @ `877208b`.

The following SQMeter routes are removed:
- `/api/sensors`, `/api/safe`, `/api/safety`, `/api/safety/history`
- `/api/sensors/tsl2591/calibrate-dark`, `/api/sensors/rg15/{test,reset-total,reboot}`
- `/api/display/{wake,blank}`, `/api/ble/{ack,forget-bonds}`
- `/api/updates/check`, `/api/updates/apply`
- `/api/i18n` (GET) and `/api/i18n/{install,upload}`

## WebSocket

`/ws/status` keeps the `/api/status` schema, every 2 s. `/ws/sensors` is removed; spec 008 adds
the roof's live stream.

## Alpaca

- Discovery on UDP 32227.
- `/management/apiversions`, `/management/v1/description` (`Manufacturer` "RoofRoof",
  `ServerName` = device name) and `/management/v1/configureddevices` = `[]`.
- `/setup` opens Settings → Device → ASCOM Alpaca.
- Requests for any device return HTTP 400 "Unknown device".

## MQTT

Under `<base>` (default `roofroof`), all as in SQMeter:
- `availability` (retained `online`/`offline`, with an LWT);
- `alerts`;
- `alerts/armed` + `alerts/armed/set`;
- `diagnostics` when enabled.

Home Assistant discovery announces the device, its connectivity and the Alerts switch.

## Status document

SQMeter's `status.schema.json` minus `sensors`, `skySensors`, `ble`, `display`, `battery`, the
RG-15/light diagnostics and the release info, plus `product: "roofroof"` in `firmware`.
