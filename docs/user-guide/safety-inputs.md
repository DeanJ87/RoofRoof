---
description: "How RoofRoof decides whether the weather is safe (SQMeter over HTTP or MQTT, the rain contact) and whether the mount is parked (N.I.N.A. and the park switches). Doubt always counts as unsafe or not parked."
---

# Safety inputs

RoofRoof keeps two answers up to date, once a second:

- **Weather**: safe, unsafe (with the reasons) or unknown.
- **Park**: parked or not parked (with the reasons).

The roof uses them to refuse an open while the weather isn't safe and a close while the mount isn't confirmed parked, and to [close by itself when the weather turns unsafe](close-on-unsafe.md). Each change is also written to the log (serial, `Safety` tag).

Doubt always lands on the safe side: a source that stops answering, answers late or answers something unreadable makes the weather **unsafe** and the mount **not parked**.

## Weather

The verdict comes from [SQMeter](https://github.com/DeanJ87/SQMeter), over HTTP, MQTT or both, plus an optional rain contact.

| Source | How it's read | Counts as unsafe when |
|---|---|---|
| SQMeter over HTTP | `GET /api/safety` every 5 s (3 s timeouts; a reply's body is read for at most 6 s) | it says `safe: false`, or any `reasonFlags` bit is set; or no good reply for the stale time (`safety.staleSeconds`, default 30 s) |
| SQMeter over MQTT | SQMeter's `availability`, `safe` and `safety` topics on this device's broker | it says unsafe; it is `offline` (SQMeter's last will); it has never said `online`; or nothing good for 150 s (SQMeter repeats its safe topic every minute). After each (re)connect the broker's stored copy only counts if it says unsafe, so an old "safe" left on the broker is never trusted |
| Rain contact (GPIO4) | read by the roof hardware | it is wet, or it can't be read |

- **Any** source that says unsafe makes the verdict unsafe.
- Rain (SQMeter reason bit 9, or the contact) and a failed rain sensor (bit 10) are always acted on, even if every other source has gone quiet.
- A single failed poll doesn't change anything: the last good reply stands until the stale time has passed. Then the source counts as unsafe, named by its latest failure ("unreachable: connection refused", "unreadable: missing reasonFlags") or "stale".
- With neither SQMeter source switched on, the verdict is unsafe ("No weather source"). The rain contact alone isn't enough.
- **Unknown** only happens just after a start or a settings change, while a source hasn't answered yet and its stale time hasn't run out. Unknown is never safe.

Every reason says what was measured and what the limit was, for example `SQMeter HTTP stale: no good reply for 75 s > 30 s`.

## Park

The mount counts as parked only when **all** of these hold:

1. N.I.N.A.'s latest reading, at most 10 s old, says the mount is connected, at park and not slewing;
2. every fitted park switch (`park.switches`: none, one on GPIO32, or two on GPIO32 and GPIO33) is closed.

Anything else is "not parked", with the reasons. When N.I.N.A. and the switches point opposite ways (N.I.N.A. says parked and a switch is open, or N.I.N.A. says not parked and every switch is closed), the result also says they **disagree**. The mount then counts as not parked, so the [close on unsafe weather](close-on-unsafe.md) waits, and its alert names the disagreement.

### N.I.N.A.

RoofRoof talks to N.I.N.A. through the **Advanced API** plugin (version 2.2.15 or newer, API v2):

1. In N.I.N.A., install **Advanced API** from the plugin list, turn the API on and note its port (default 1888).
2. Set `park.nina.url` to `http://<pc running N.I.N.A.>:<port>`, for example `http://192.168.1.20:1888`.

RoofRoof reads `GET /v2/api/equipment/mount/info` every 2 s and, when the close sequence asks, sends `GET /v2/api/equipment/mount/park`. N.I.N.A. answers a park request at once and parks in the background; RoofRoof only trusts the park once mount info says `AtPark`.

## Setting it up

Set the sources in **Settings → Roof → Weather sources** and the park in **Settings → Roof → Mount park**, or with the settings API (fields in [Configuration](configuration.md#safety-and-park)):

```bash
curl -X POST http://roofroof.local/api/config -H "Content-Type: application/json" -d '{
  "safety": { "http": { "enabled": true, "url": "http://sqmeter.local/api/safety" } },
  "park":   { "nina": { "url": "http://192.168.1.20:1888" }, "switches": "none" }
}'
```

Then watch the log: within a few seconds it shows `Weather safe` (or the reasons it isn't) and `Park: parked` or `Park: not parked: ...`.

!!! warning "Plain http on the local network"
    SQMeter and N.I.N.A. are reached over plain `http://`; neither offers TLS. Keep them, like RoofRoof, on a trusted network.
