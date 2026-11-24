---
description: "How RoofRoof decides whether the weather is safe (SQMeter over HTTP or MQTT, rain from its gauge) and whether the mount is parked (N.I.N.A., the park switches, or both). Doubt always counts as unsafe or not parked."
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
| SQMeter over HTTP | `GET /api/safety` every 5 s at SQMeter's address (3 s timeouts, and 3 s for an https handshake; a reply's body is read for at most 6 s) | it says `safe: false`, or any `reasonFlags` bit is set; or no good reply for the stale time (`safety.staleSeconds`, default 30 s) |
| SQMeter over MQTT | SQMeter's `availability`, `safe` and `safety` topics on this device's broker | it says unsafe; it is `offline` (SQMeter's last will); it has never said `online`; or nothing good for 150 s (SQMeter repeats its safe topic every minute). After each (re)connect the broker's stored copy only counts if it says unsafe, so an old "safe" left on the broker is never trusted |

- **Any** source that says unsafe makes the verdict unsafe.
- Rain (SQMeter reason bit 9) and a failed rain sensor (bit 10) are always acted on, even if every other source has gone quiet.
- A single failed poll doesn't change anything: the last good reply stands until the stale time has passed. Then the source counts as unsafe, named by its latest failure ("unreachable: connection refused", "unreadable: missing reasonFlags", "unreadable: SQMeter wants a password", "unreadable: SQMeter refused the password") or "stale". If SQMeter has a password, enter its user name and password with its address ([A password-protected SQMeter](close-on-unsafe.md#a-password-protected-sqmeter)).
- **SQMeter's address** (`safety.http.url`) is just the address: `192.168.1.50`, `sqmeter.local` or `http://192.168.1.50`. RoofRoof adds `/api/safety` itself (and SQMeter's other paths for the [site data](close-on-unsafe.md#site-from-sqmeter)). A URL ending in one of SQMeter's API paths (`/api/safety`, `/api/sensors`, `/api/status`, `/api/config`) is taken as its address; any other path is refused. **Test**, next to the address, reads SQMeter once at the address and login typed and says what came back: safe, unsafe and why, or why not.
- When the address answers but not like SQMeter's `/api/safety` (another of SQMeter's pages, another device, a 404), the source says so ("that address answered HTTP 404, not like SQMeter's /api/safety"), also in the first seconds when it would otherwise be waiting for a first reply.
- **https:** an `https://` address is read over TLS. The link is encrypted, but SQMeter's certificate isn't checked: a device on your network has a self-signed one. Only one HTTPS request runs at a time on the device, and none while a firmware or web UI upload is written; when a poll can't start within a second (an alert is being sent, say), it fails and counts toward the stale time, so a long hold makes the source unsafe, never safe. https to an IPv6 address isn't supported; use a host name.
- With neither SQMeter source switched on, the verdict is unsafe ("No weather source").
- **Unknown** only happens just after a start or a settings change, while a source hasn't answered yet and its stale time hasn't run out. Unknown is never safe.

Every reason says what was measured and what the limit was, for example `SQMeter HTTP stale: no good reply for 75 s > 30 s`.

!!! warning "Rain is seen only through SQMeter"
    Earlier firmware could also read a local rain contact; it has been removed (constitution 4.0.0), and an old `safety.rainContact` setting is dropped when the settings load. An SQMeter that stops answering, answers late or unreadably, or reports its rain gauge failed still makes the verdict unsafe, exactly as before. What is no longer backed up is a gauge that misses rain while SQMeter itself stays healthy: keep the gauge clean and check it on SQMeter's side.

!!! note "Test box exception"
    For a box with no weather source yet, the owner can switch on the weather bypass of [test box mode](test-box.md): "No weather source" then counts as safe, until a deadline. It never stands in for a source you have set up.

## Park

What confirms the park is the **park source** (`park.source`, **Settings → Roof → Mount park**, or the setup wizard's question "How does RoofRoof know the mount is parked?"):

| Park source | The mount counts as parked when |
|---|---|
| **N.I.N.A. only** (`nina`, the default) | N.I.N.A.'s latest reading, at most 10 s old, says the mount is connected, at park and not slewing. |
| **N.I.N.A. and park switches** (`ninaAndSwitches`) | that, **and** every fitted park switch is closed. Both must agree. |
| **Park switches only** (`switches`) | every fitted park switch is closed (with two, both), and no N.I.N.A. reading from the last 10 s says the mount is slewing. N.I.N.A. is not needed. |

`park.switches` says how many switches are fitted: none, one (park switch A) or two (A and B); their pins are on the [Pins](pins.md) page. N.I.N.A. only uses none; the two switch sources need one or two, and a save that mixes them up is refused. Settings saved before the park source existed keep their rule: no switch means N.I.N.A. only, one or two mean N.I.N.A. and switches.

### Park switches only

With the switches as the only source, N.I.N.A.'s address is optional. Without one, the [close on unsafe weather](close-on-unsafe.md) asks nobody to park and only waits for the switches (up to `park.timeoutSeconds`); with one, it still asks N.I.N.A. to park first (unless **May ask N.I.N.A. to park** is off). A reading from N.I.N.A. counts only against the park: if it says the mount is slewing, the mount is not parked whatever the switches say. N.I.N.A. being unreachable, stale, disconnected or reporting "not at park" doesn't hold the roof open.

A switch is a single wire, and its faults are not symmetric:

| Fault | What the switch reads | Result |
|---|---|---|
| Broken wire, loose connector | open | not parked (safe) |
| Short to ground, switch stuck closed | closed | **parked, whatever the mount does** |

So RoofRoof guards against the second kind:

- **Wiring**: a switch closes to ground when the mount is at park (normally open). Normally-closed wiring is not supported: it would turn the commoner fault, a broken wire, into "parked". See [Wiring](../hardware/wiring.md#which-way-round-the-switches-read).
- **Two switches recommended**: fit two, at different points of the mount and on separate cables. A short or a stuck switch then disagrees with the other one, and the mount counts as not parked. With one switch, Settings and the wizard warn that such a fault can't be told from a parked mount.
- **Stuck-switch check**: RoofRoof counts, for each fitted switch, the roof openings since the switch last read open. When a switch hasn't opened over **3 roof openings in a row**, Open is refused: "Park switch 1 hasn't opened in 3 roof openings (stuck or shorted?): move the mount off park once". The count clears as soon as the switch reads open, and it is kept across restarts. On a normal night the mount leaves its park, so this never shows; three cloudy-night openings in a row without unparking will ask for that one check. The roof is closed whenever this refuses, so it never leaves the roof open in the rain. It counts only with the park switches as the source, and only full openings (not a calibration run); an attended jog or teach toward open isn't refused by it. The status shows the counts (`roof.parkSwitches.openings`) and the refusal (`roof.parkSwitches.check`).

!!! warning "The risk you take with one switch"
    A short or a stuck switch that happens during a night, with one switch and no N.I.N.A. in the rule, reads "parked" while the mount is not, and the roof can close onto it. The stuck-switch check catches a fault that lasts, at the next openings, not one that starts mid-night. Two switches on separate cables, or N.I.N.A. and park switches, close that gap.

Anything else is "not parked", with the reasons. A close starts only on a park confirmed this way.

While the roof closes (and during every calibration leg and jog), the park is checked again at every control step, and anything that says the mount isn't parked stops the motor with the latched fault "Park lost during the close": N.I.N.A. saying not at park, slewing or not connected; N.I.N.A. and the switches disagreeing; a fitted switch opening or becoming unreadable; N.I.N.A.'s address being removed or changed; or the safety inputs not being handed to the roof for 5 s. A **missed N.I.N.A. reply** on its own (no answer, a reply that can't be read, an API error) is not a report that the mount moved: the close carries on with N.I.N.A.'s last good reading, as long as that reading said parked, agrees with the switches as they read now, and is no more than 10 s old. Once it is older, the close stops with "Park lost during the close: N.I.N.A. not read within its freshness limit". The roof's control task measures that age itself, so the limit holds even when the rest of the device is slow. The log says when a close rides on the last reading (`N.I.N.A. read failed: a move under way is riding on a parked reading 2.1 s old (limit 10 s)`) and when that ends. When N.I.N.A. and the switches point opposite ways (N.I.N.A. says parked and a switch is open, or N.I.N.A. says not parked and every switch is closed), the result also says they **disagree**. The mount then counts as not parked, so the [close on unsafe weather](close-on-unsafe.md) waits, and its alert names the disagreement.

!!! note "Test box exception"
    For a box with no telescope yet, the park bypass of [test box mode](test-box.md) acts as a park source saying "no telescope under this roof", until a deadline. It is refused while a N.I.N.A. address is set or with park switches only (there is no N.I.N.A. to stand in for), and fitted park switches still count.

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
  "safety": { "http": { "enabled": true, "url": "http://sqmeter.local" } },
  "park":   { "source": "nina", "nina": { "url": "http://192.168.1.20:1888" }, "switches": "none" }
}'
```

Then watch the log: within a few seconds it shows `Weather safe` (or the reasons it isn't) and `Park: parked` or `Park: not parked: ...`.

!!! warning "Plain http on the local network"
    SQMeter and N.I.N.A. are reached over plain `http://`; neither offers TLS. Keep them, like RoofRoof, on a trusted network.
