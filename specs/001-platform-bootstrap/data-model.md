# Data Model: Platform Bootstrap

## Config (persisted in NVS namespace `roofroof`)

The shape and validation are SQMeter's (SQMeter spec 011). Only the sections listed here survive.
Ranges are in `test/fixtures/config-ranges.json`, shared by the device and the browser.

| Section | Fields | Notes |
|---|---|---|
| `deviceName` | string | Default "RoofRoof". Used as the Alpaca `ServerName`. |
| `wifi` | ssid, password (secret), hostname, autoReconnect, back-off delays, mdns | Default hostname `roofroof`. An empty SSID is rejected while one is saved (SQMeter 011 FR-009). |
| `ntp` | servers, interval | NTP is the only time source. |
| `timeZone` | POSIX TZ | |
| `ota` | enabled, password (secret) | ArduinoOTA, opt-in. |
| `auth` | enabled, username, password (secret) | Protects every state-changing route. |
| `mqtt` | enabled, host, port, user, password (secret), topic (default `roofroof`), interval, `publish.diagnostics`, `homeAssistant{enabled, discoveryPrefix}` | Spec 008 adds roof publish groups. |
| `alpaca` | enabled | The device list is empty until spec 006. |
| `alerts` | channels (pushover, ntfy, webhook, mqtt), cooldown, sendMode, clientSilence, events{`test`, client events, `alertsResumed`} | Each event has level 0-4, sound, title and message, as in SQMeter 008. |

Removed relative to SQMeter: `gps`, `rain`, `sensor`, `sky*`, `cloudDetection`, `ble`, `wind`,
`location`, `display`, `battery`, `language`, the time-source priority, the alpaca thresholds and
the sensor/safety/battery alert events.

**Limits**: persisted JSON ≤ 5100 bytes, alerts JSON ≤ 3900 bytes (unchanged).

**Migration**: missing keys keep their current value, and wrong-typed stored values keep the
default (SQMeter behaviour). There are no legacy keys.

## Firmware image marker

See [contracts/firmware-marker.md](contracts/firmware-marker.md).

| Field | Values | Meaning |
|---|---|---|
| product | `roofroof` | Refuses images for another product |
| layout | `l2` | Partition layout (4 MB, two OTA slots) |
| build | `standard` | The only build |

## Driver outputs (`lib/DriverLogic`)

| Command | EN | RPWM duty | LPWM duty |
|---|---|---|---|
| Off | low | 0 | 0 |
| Forward(d) | high | d | 0 |
| Reverse(d) | high | 0 | d |

Invariant: RPWM and LPWM are never both > 0. A duty of 0 with Forward/Reverse gives "stopped,
enabled" (used for braking in spec 002). In 001 only `Off` is ever applied.

## Heritage entry (`docs/development/heritage.md`)

| Field | Example |
|---|---|
| RoofRoof path | `lib/CaptiveDns/` |
| SQMeter path | `lib/CaptiveDns/` |
| Source commit | `877208b` |
| State | unchanged / adapted / RoofRoof-only |
| Upstream candidate | yes / no |
| Why it differs | "product field in the marker" |
