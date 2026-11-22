# Research: Site Data from SQMeter

Checked against SQMeter `main` at 34d3388 (v0.4.0-alpha.2+dev), its `docs/api/openapi.json`,
`lib/DeviceCore/src/DeviceCore.cpp` and `lib/ConfigModel/src/ConfigJsonOut.cpp`, and its stored
settings fixtures `test/fixtures/config-releases/` (v0.1.0 to v0.3.3).

## R1. Where SQMeter's location comes from

- **Decision**: `GET /api/status` → `sky`. Fields (SQMeter's `writeSky`): `locationSource` is
  `"gps"`, `"manual"` or `"none"`; `latitude` and `longitude` (rounded to 4 decimals) are present
  only when the source isn't `none`; `nightKnown`, `isNight` and `sunAltitudeDeg` follow the clock.
  The OpenAPI schema requires only `locationSource` and `nightKnown`.
- **Rationale**: the status carries the location SQMeter actually uses: a GPS fix when it has one,
  else its typed location. `GET /api/config`'s `location` has only the typed one.
- **Versions**: the `sky` section arrived with SQMeter v0.2.0 (`git log -S locationSource`:
  b1d382e1, 2026-10-08). Older firmware has no `sky`: read as "no location".
- **Alternatives**: SQMeter's MQTT (would need RoofRoof's broker and SQMeter's publish group);
  `/api/config` (no GPS).

## R2. SQMeter's address

- **Decision**: the scheme, host and port of `safety.httpUrl` (parsed by `Net::parseHttpUrl`, IPv6
  literals kept in brackets), with SQMeter's own path (`/api/status`, `/api/config`). Only
  `http://` (as the weather source).
- **Rationale**: RoofRoof already has SQMeter's safety URL; SQMeter serves every route from `/`.

## R3. CORS and who fetches the settings

- **Decision**: the device fetches `GET /api/config` (`POST /api/sqmeter/site`).
- **Rationale**: SQMeter sets no `Access-Control-Allow-Origin` (no match for "Access-Control" or
  "DefaultHeaders" in its `src/` and `lib/`), so a browser on RoofRoof's origin can't read the
  reply. The device has no such limit.
- **Alternatives**: a browser fetch (blocked by CORS); a proxy route passing SQMeter's whole
  document through (would forward everything, including whatever an old SQMeter left unmasked).

## R4. Masking

- **Decision**: treat every secret field as "set" when its value is non-empty, and never read its
  value further. Fields: `mqtt.password`, `alerts.ntfy.token`, `alerts.pushover.userKey`,
  `alerts.pushover.appToken` (also masked by SQMeter: `wifi.password`, `ota.password`,
  `auth.password`, `ble.passkey`, `alerts.webhook.authHeader`, never read).
- **Rationale**: SQMeter's `secretOut()` returns `"********"` for a non-empty secret and `""` for
  none (`ConfigJsonOut.cpp`). The Pushover user key is masked too, so Pushover can't be copied.
  The parse filter keeps each secret field (ArduinoJson copies its text into the short-lived
  parse document, freed when the fetch ends), the parser reads only whether it is empty, and the
  site document, the reply and the logs carry only booleans for them.

## R5. Older SQMeter settings

| Release | `location` | `ntp.timezone` | top-level `timezone` | `alerts.ntfy` | `language` |
|---|---|---|---|---|---|
| v0.1.x | no | no | yes | no | no |
| v0.2.0-beta.1 | yes | no | yes | yes | no |
| v0.2.0-beta.2 on | yes | yes | no | yes | no |
| v0.3.x on | yes | yes | no | yes | yes |

- **Decision**: each part is optional; `ntp.timezone` first, else top-level `timezone`.

## R6. Size and parsing

- **Decision**: parse while reading, with an ArduinoJson filter, into a 1 KB (status) or 2 KB
  (settings) document. A small reader wraps the socket with a total deadline (3 s for the
  location, 5 s for the copy), so a slow server can't hold the poller longer than that.
- **Rationale**: SQMeter's status is about 3.5 KB in its OpenAPI example and grows with sensors;
  its settings can carry custom alert templates (each up to a few hundred bytes). The poller's
  existing body buffer is capped at 8 KB; a filtered parse needs no body buffer at all.

## R7. The read's schedule and the poller's longest pass

- **Decision**: `Site::LocationSchedule`: first read 15 s after the poller starts or at once after
  a change of the address or the source; then 10 min after a good read, 2 min after a failed one.
  It runs only in a pass where the safety poll didn't run, and only while not busy. While SQMeter
  is unreachable it costs one bounded attempt every 2 minutes, in a pass of its own.
- **Rationale**: the poller polls N.I.N.A. every 2 s (each up to 6 s) and SQMeter's safety every
  5 s (up to 6 s). Today's longest pass is N.I.N.A. plus SQMeter's safety. A read (≤ 3 s) never
  shares a pass with the safety poll, and none starts when the safety poll is due within 3 s, so
  it never delays that poll and the longest pass stays N.I.N.A. plus 6 s at most. As for the
  safety poll, the bound covers connecting and the body: a host name lookup has no timeout of its
  own, and a server that sends its header lines slowly can hold a read longer.
- **Busy**: the roof moving (`RoofControl::moving`), a close waiting (`closeWaiting`), or the
  close sequence working (`SequenceState::Working`); set by the main loop each pass, as spec 008's
  MQTT keeps its writes down then.

## R8. Keeping the location

- **Decision**: in the settings (`location.latitude`, `longitude`, `set`), with `location.sqmeter.gps`
  and `location.sqmeter.savedAt`. Saved only on a change of ≥ 0.001° (about 110 m), a different GPS
  flag, or nothing set yet. Saved from the main loop through the same path as the Wi-Fi join's save
  (`PlatformGuard::FlashWrite`, then the settings save callback), so spec 010's flash-write rules
  apply; a refused save is tried again on the next pass.
- **Rationale**: daylight already reads the settings (`Daylight::settingsFrom`), so spec 013 is
  untouched; a backup carries the location; NVS wear is bounded by real moves only.
- **Alternatives**: a separate NVS key (more plumbing into daylight and the status; the settings
  page couldn't show the value).

## R9. The copy's task

- **Decision**: a short-lived FreeRTOS task (`site_job`, core 1 beside the loop task, priority 1, 6 KB) per request, as
  spec 008's backup job; the request is paused and answered from the task through its weak
  pointer. One at a time (an atomic flag).
- **Rationale**: constitution VI keeps the async TCP task off the network. The poller's loop must
  keep N.I.N.A. fresh, so a 5 s fetch doesn't belong there.

## R10. Clearing secrets on a copied server

- **Decision**: the page sends `mqtt.password: null` when the copied broker, port or user name
  differs from the saved one, and `alerts.ntfy.token: null` when the copied ntfy server differs.
  ConfigModel already clears a secret sent as `null` (`assignSecret`).
- **Rationale**: the saved secret was saved for a server; keeping it would send it to SQMeter's
  broker or server, which may be someone else's (constitution VIII; the same rule as
  `Config::mqttTestPassword`).

## R11. Keeping in step (sync)

- **Decision**: re-read SQMeter's `GET /api/config` through the copy's filter on the poller, with a
  second `LocationSchedule` (same rules, own times); the location read has priority and a pass does
  at most one SQMeter read. The loop applies a good read under `configSaveLock` (try-lock) and the
  flash-write guard: `Site::syncFromSite` applies each followed group to a copy and checks it with
  `Config::validateChange` (the save's validation plus the UI-only host and URL rules).
- **Rationale**: the same lean, non-blocking path as the location; the filter keeps the parsed
  document to the few fields used whatever SQMeter's alert templates hold (20 templates of 480
  characters, over 10 KB, parse into 90 bytes on the host; the document is 3 KB). Peak heap is
  logged per read (`SQMeter settings read: … heap free … lowest …`, debug) and checked on the bench.
- **Writes**: a save only when a group's values change, or once when a following group is first
  confirmed (`syncedAt` set); so an unchanged SQMeter writes nothing.
- **MQTT roof commands**: the group is held while they are on (constitution VIII: commands only from
  the configured broker; a broker changed from elsewhere would change who may move the roof).
- **Alternatives**: a sync per group on its own schedule (three reads instead of one); syncing from
  the browser (CORS); a separate NVS record of synced values (the settings are where every
  subsystem already looks).
