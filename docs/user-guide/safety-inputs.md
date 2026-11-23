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

!!! note "Test box exception"
    For a box with no weather source yet, the owner can switch on the weather bypass of [test box mode](test-box.md): "No weather source" then counts as safe, until a deadline. It never stands in for a source you have set up, and a wet or unreadable rain contact still makes the weather unsafe.

## Park

The mount counts as parked only when **all** of these hold:

1. N.I.N.A.'s latest reading, at most 10 s old, says the mount is connected, at park and not slewing;
2. every fitted park switch (`park.switches`: none, one on GPIO32, or two on GPIO32 and GPIO33) is closed.

The park switches are optional. With none fitted (the default) N.I.N.A.'s park alone confirms it; fitted switches are a second check on the mount itself, and both must agree.

Anything else is "not parked", with the reasons. A close starts only on a park confirmed this way.

While the roof closes (and during every calibration leg and jog), the park is checked again at every control step, and anything that says the mount isn't parked stops the motor with the latched fault "Park lost during the close": N.I.N.A. saying not at park, slewing or not connected; N.I.N.A. and the switches disagreeing; a fitted switch opening or becoming unreadable; N.I.N.A.'s address being removed or changed; or the safety inputs not being handed to the roof for 5 s. A **missed N.I.N.A. reply** on its own (no answer, a reply that can't be read, an API error) is not a report that the mount moved: the close carries on with N.I.N.A.'s last good reading, as long as that reading said parked, agrees with the switches as they read now, and is no more than 10 s old. Once it is older, the close stops with "Park lost during the close: N.I.N.A. not read within its freshness limit". The roof's control task measures that age itself, so the limit holds even when the rest of the device is slow. The log says when a close rides on the last reading (`N.I.N.A. read failed: a move under way is riding on a parked reading 2.1 s old (limit 10 s)`) and when that ends. When N.I.N.A. and the switches point opposite ways (N.I.N.A. says parked and a switch is open, or N.I.N.A. says not parked and every switch is closed), the result also says they **disagree**. The mount then counts as not parked, so the [close on unsafe weather](close-on-unsafe.md) waits, and its alert names the disagreement.

!!! note "Test box exception"
    For a box with no telescope yet, the park bypass of [test box mode](test-box.md) acts as a park source saying "no telescope under this roof", until a deadline. It is refused while a N.I.N.A. address is set, and fitted park switches still count.

### N.I.N.A.

RoofRoof talks to N.I.N.A. through the **Advanced API** plugin (version 2.2.15 or newer, API v2):

1. In N.I.N.A., install **Advanced API** from the plugin list, turn the API on and note its port (default 1888).
2. Set `park.nina.url` to `http://<pc running N.I.N.A.>:<port>`, for example `http://192.168.1.20:1888`.

RoofRoof reads `GET /v2/api/equipment/mount/info` every 2 s and, when the close sequence asks, sends `GET /v2/api/equipment/mount/park`. N.I.N.A. answers a park request at once and parks in the background; RoofRoof only trusts the park once mount info says `AtPark`.

!!! danger "A RoofRoof parks the mount its N.I.N.A. drives"
    Whenever the weather turns unsafe, RoofRoof asks the N.I.N.A. at `park.nina.url` to park. A bench or spare RoofRoof pointed at the N.I.N.A. that is imaging with your real mount will park that mount mid-session. On a bench, leave the address empty and use [test box mode](test-box.md), or turn off **May ask N.I.N.A. to park** (`park.requestPark`, on by default): the automatic close then only waits for the park.

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
