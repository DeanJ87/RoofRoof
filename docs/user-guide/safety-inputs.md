---
description: "How RoofRoof decides whether the weather is safe (SQMeter over HTTP or MQTT, rain from its gauge) and whether the mount is parked (N.I.N.A. and the park switches). Doubt always counts as unsafe or not parked."
---

# Safety inputs

RoofRoof keeps two answers up to date, once a second:

- **Weather**: safe, unsafe (with the reasons) or unknown.
- **Park**: parked or not parked (with the reasons).

The roof uses them to refuse an open while the weather isn't safe and a close while the mount isn't confirmed parked, and to [close by itself when the weather turns unsafe](close-on-unsafe.md). Each change is also written to the log (serial, `Safety` tag).

Doubt always lands on the safe side: a source that stops answering, answers late or answers something unreadable makes the weather **unsafe** and the mount **not parked**.

## Weather

The verdict comes from [SQMeter](https://github.com/DeanJ87/SQMeter), over HTTP, MQTT or both. Rain comes from SQMeter too: its RG-15 rain gauge sets `reasonFlags` bit 9 when it rains and bit 10 when the gauge has failed. Nothing about the weather is wired to the roof controller.

| Source | How it's read | Counts as unsafe when |
|---|---|---|
| SQMeter over HTTP | `GET /api/safety` every 5 s (3 s timeouts; a reply's body is read for at most 6 s) | it says `safe: false`, or any `reasonFlags` bit is set; or no good reply for the stale time (`safety.staleSeconds`, default 30 s) |
| SQMeter over MQTT | SQMeter's `availability`, `safe` and `safety` topics on this device's broker | it says unsafe; it is `offline` (SQMeter's last will); it has never said `online`; or nothing good for 150 s (SQMeter repeats its safe topic every minute). After each (re)connect the broker's stored copy only counts if it says unsafe, so an old "safe" left on the broker is never trusted |

- **Any** source that says unsafe makes the verdict unsafe.
- Rain (SQMeter reason bit 9) and a failed rain sensor (bit 10) are always acted on, even if every other source has gone quiet.
- A single failed poll doesn't change anything: the last good reply stands until the stale time has passed. Then the source counts as unsafe, named by its latest failure ("unreachable: connection refused", "unreadable: missing reasonFlags") or "stale".
- With neither SQMeter source switched on, the verdict is unsafe ("No weather source").
- **Unknown** only happens just after a start or a settings change, while a source hasn't answered yet and its stale time hasn't run out. Unknown is never safe.

Every reason says what was measured and what the limit was, for example `SQMeter HTTP stale: no good reply for 75 s > 30 s`.

!!! warning "Rain is seen only through SQMeter"
    Earlier firmware could also read a local rain contact; it has been removed (constitution 3.0.0), and an old `safety.rainContact` setting is dropped when the settings load. An SQMeter that stops answering, answers late or unreadably, or reports its rain gauge failed still makes the verdict unsafe, exactly as before. What is no longer backed up is a gauge that misses rain while SQMeter itself stays healthy: keep the gauge clean and check it on SQMeter's side.

## Park

The mount counts as parked only when **all** of these hold:

1. N.I.N.A.'s latest reading, at most 10 s old, says the mount is connected, at park and not slewing;
2. every fitted park switch (`park.switches`: none, one (park switch A), or two (A and B); their pins are on the [Pins](pins.md) page) is closed.

The park switches are optional. With none fitted (the default) N.I.N.A.'s park alone confirms it; fitted switches are a second check on the mount itself, and both must agree.

Anything else is "not parked", with the reasons. A close starts only on a park confirmed this way.

While the roof closes (and during every calibration leg and jog), the park is checked again at every control step, and anything that says the mount isn't parked stops the motor with the latched fault "Park lost during the close": N.I.N.A. saying not at park, slewing or not connected; N.I.N.A. and the switches disagreeing; a fitted switch opening or becoming unreadable; N.I.N.A.'s address being removed or changed; or the safety inputs not being handed to the roof for 5 s. A **missed N.I.N.A. reply** on its own (no answer, a reply that can't be read, an API error) is not a report that the mount moved: the close carries on with N.I.N.A.'s last good reading, as long as that reading said parked, agrees with the switches as they read now, and is no more than 10 s old. Once it is older, the close stops with "Park lost during the close: N.I.N.A. not read within its freshness limit". The roof's control task measures that age itself, so the limit holds even when the rest of the device is slow. The log says when a close rides on the last reading (`N.I.N.A. read failed: a move under way is riding on a parked reading 2.1 s old (limit 10 s)`) and when that ends. When N.I.N.A. and the switches point opposite ways (N.I.N.A. says parked and a switch is open, or N.I.N.A. says not parked and every switch is closed), the result also says they **disagree**. The mount then counts as not parked, so the [close on unsafe weather](close-on-unsafe.md) waits, and its alert names the disagreement.

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
