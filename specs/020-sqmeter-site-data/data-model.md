# Data Model: Site Data from SQMeter

## LocationConfig (lib/ConfigModel, persisted)

| Field | Type | JSON | Rule |
|---|---|---|---|
| `source` | `Source` (`Sqmeter`, `Manual`) | `location.source`: `"sqmeter"` / `"manual"` | default `Sqmeter`; a section without it: `Manual` if `set`, else `Sqmeter` |
| `set` | bool | `location.set` | as spec 013 |
| `latitude` | double | `location.latitude` | −90..90, finite |
| `longitude` | double | `location.longitude` | −180..180, finite |
| `gps` | bool | `location.sqmeter.gps` | device-owned: the kept value was SQMeter's GPS fix |
| `savedAt` | uint32 | `location.sqmeter.savedAt` | device-owned: Unix seconds of the last save from SQMeter; 0 = never or no clock |

API save (strict): with the source `sqmeter` before and after and a location already kept, `set`, `latitude`, `longitude`,
`gps` and `savedAt` keep the device's values. `location.sqmeter` is never read from the API. An
unknown `source` is refused ("Location: source must be sqmeter or manual").

## Site::LocationRead (lib/SafetyInputLogic, RAM)

| Field | Type | Meaning |
|---|---|---|
| `problem` | `Problem`: `None`, `Waiting`, `NoAddress`, `Unreachable`, `Unreadable`, `NoLocation` | `None` = good |
| `latitude`, `longitude` | double | when good |
| `gps` | bool | `sky.locationSource == "gps"` |
| `atMs` | uint64 | poller time of the read |
| `goodAtMs` | uint64 | the last good read in this run (0 none) |
| `detail` | string | why it failed ("HTTP 404", "connection refused") |

## Site::LocationSchedule (pure)

Input per pass: `nowMs`, `enabled` (source `sqmeter` and an address), `busy`, `safetyPolled` (this
pass), `generation` (bumped by a change of address or source). State: the next due time, the
generation seen. `due()` is true when enabled, not busy, the safety poll didn't run this pass and isn't due within
`READ_MS` (3 s; `safetyNextMs`, 0 when that source is off), and
`nowMs` has reached the due time.
`done(good, nowMs)` sets the next due time (+10 min good, +2 min failed). A new generation makes it
due at once; the first is due at `start + 15 s`.

## Site::locationToSave (pure)

`std::optional<LocationConfig> locationToSave(const LocationConfig &current, const LocationRead &read, int64_t epoch)`:
nothing unless `current.source == Sqmeter` and the read is good; then a copy with the read's value,
`set = true`, `gps`, and `savedAt = epoch` (0 when the clock isn't set) when `!current.set`, the GPS
flag differs, or `|Δlat| ≥ 0.001` or `|Δlon| ≥ 0.001`.

## Site::SiteConfig (pure, from SQMeter's GET /api/config)

| Part | Fields | Present when |
|---|---|---|
| `device` | `name` (`deviceName`; shown only, never copied) | always |
| `location` | `set`, `latitude`, `longitude` | `location` object |
| `time` | `timezone`, `server1`, `server2` | `ntp` object or top-level `timezone` |
| `mqtt` | `broker`, `port`, `username`, `topic`, `passwordSet` | `mqtt` object |
| `ntfy` | `server`, `topic`, `tokenSet` | `alerts.ntfy` object |
| `pushover` | `userKeySet`, `appTokenSet` | `alerts.pushover` object |

`writeSiteDocument` writes `{"address","device","location","time","mqtt","ntfy","pushover"}` with
absent parts as `null`. No secret text is ever stored in `SiteConfig`.

## Status `location` section (GET /api/status, /ws/status)

```json
"location": {
  "source": "sqmeter",
  "set": true,
  "gps": true,
  "savedAt": 1763540000,
  "readAgeSeconds": 180,
  "problem": null
}
```

`savedAt` and `readAgeSeconds` are `null` when unknown; `problem` is one of `"noAddress"`,
`"waiting"`, `"unreachable"`, `"unreadable"`, `"noLocation"` or `null`. With `manual`, `gps`,
`readAgeSeconds` and `problem` are `false`/`null`.

## Copy row (web, `web/src/lib/sqmeterCopy.ts`)

| Field | Meaning |
|---|---|
| `id` | `location`, `timezone`, `ntp`, `mqttBroker`, `weatherTopic`, `ntfy` |
| `ours`, `theirs` | display strings |
| `changes` | `[path, value][]` applied when ticked (incl. `null` secrets to clear) |
| `secrets` | i18n keys of what the person must enter (`mqttPassword`, `ntfyToken`) |

Plus `notes`: `pushover` when SQMeter has Pushover keys set ("enter the user key and app token").

## SqmeterSyncConfig (lib/ConfigModel, persisted)

```json
"sqmeterSync": {
  "time":   { "follow": true, "syncedAt": 0 },
  "mqtt":   { "follow": true, "syncedAt": 0 },
  "alerts": { "follow": true, "syncedAt": 0 }
}
```

| Group | Fields it owns while following | Secret cleared on a server change |
|---|---|---|
| `time` | `ntp.timezone`, `ntp.server1`, `ntp.server2` | none |
| `mqtt` | `mqtt.broker`, `mqtt.port`, `mqtt.username`, `safety.mqttTopic` | `mqtt.password` |
| `alerts` | `alerts.ntfyServer`, `alerts.ntfyTopic` | `alerts.ntfyToken` |

`syncedAt` (Unix seconds of the last change from SQMeter, 1 without a clock, 0 never) is the
device's and never read from the API. Defaults: every `follow` true. Stored settings without the
section: a group follows only when its fields still have their defaults (`UTC0`, `pool.ntp.org`,
`time.nist.gov`; no broker or user name, port 1883, weather topic `sqmeter`; ntfy `https://ntfy.sh`
with no topic).

## Site::ConfigRead, Site::SyncOutcome, Site::SyncState (lib/SafetyInputLogic SqmeterSync.h)

- `ConfigRead`: the last read of SQMeter's settings (`problem`, `site`, `atMs`, `goodAtMs`, `detail`);
  a failed read keeps the last good site and its time.
- `syncFromSite(live, site, epoch, clockSet)` → `SyncOutcome {next, groups[3] {changed, refused},
  marked}`: each followed group applied to a copy and checked with `Config::validateChange`; MQTT held
  while `mqtt.roofCommands`.
- `keepFollowed(live, next)`: the save-time keep rule for the location and every synced group.
- `secretsNeeded(config, site, group)`: `mqttPassword`, `ntfyToken`.
- `SyncState`: the last refusal per group (RAM, for the status).

## Status `sync` section

```json
"sync": {
  "problem": null,
  "readAgeSeconds": 180,
  "time":   { "follow": true,  "syncedAt": 1763540000, "refused": null, "needs": [] },
  "mqtt":   { "follow": true,  "syncedAt": 1763540000, "refused": null, "needs": ["mqttPassword"] },
  "alerts": { "follow": false, "syncedAt": null,       "refused": null, "needs": [] }
}
```

`problem` (`noAddress`, `waiting`, `unreachable`, `unreadable`, or `null`) is `null` while no group
follows.
