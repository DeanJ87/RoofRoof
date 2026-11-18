# Contract: Jog and Teach Routes

Every `POST` calls `requireAuth` and needs the header `X-RoofRoof: 1` (403 without it). A refusal
is `409 {"success": false, "reason": "<machine name>", "error": "<text>"}`; a malformed body is
`400 {"error": "..."}`.

## POST /api/roof/jog

Start a jog, or renew one.

```json
{ "direction": "open", "attended": true }
{ "direction": "open", "attended": true, "id": 7 }
```

- Without `id`: starts a jog toward `direction` (`open` or `close`). `200`:
  `{"success": true, "state": "opening", "id": 7, "holdMs": 500, "keepAliveMs": 150, "limitMs": 30000}`.
- With `id`: renews jog `id` for another `holdMs`. `200` as above; `409 jogEnded` when that jog is
  no longer running (it never starts a new one).
- Release: the same body with `"release": true` stops jog `id` only (`409 jogEnded` if it has
  ended). The answer to a start carries `limitMs`, the jog's hard timeout.
- Start refusals: `notAttended`, `calibrating`, `closeSequence`, `faultLatched`, `moving`,
  `closeWaiting`, `settingsPending`, `weatherNotSafe`, `daylight`, `mountNotParked`, `busy`,
  `hotspot`, `restarting`, `notStarted`, or the motion logic's (`supplyLow`, `supplyUnreadable`,
  `currentUnreadable`, `hallNotSettled`, `settingsInvalid`, `notAPerson`).

## GET /api/roof/teach

Open (readings only).

```json
{
  "jog": { "active": true, "id": 7, "direction": "open", "elapsedMs": 1240, "limitMs": 30000, "holdMs": 500, "keepAliveMs": 150 },
  "rest": { "ready": false, "counts": null },
  "marks": {
    "closed": { "count": 50, "mean": 712.4, "min": 709, "max": 716, "noise": 1.6 },
    "open": null,
    "partWay": { "count": 50, "mean": 2050.1, "min": 2047, "max": 2053, "noise": 1.2 }
  },
  "proposal": null
}
```

`jog.direction` is `null` when no jog is active. `proposal` is `null` until all three marks exist,
then either `{"ok": true, "hallHighEnd": "open", "hallHighCounts": 2730, "hallLowCounts": 1381}` or
`{"ok": false, "reason": "samePole" | "weakEnd" | "noRoom", "text": "..."}`.

## POST /api/roof/teach/mark

```json
{ "at": "closed" }
```

`at`: `closed`, `open` or `partWay`. `200 {"success": true}`. Refusals: `notAtRest` (moving,
jogging, or less than a second at rest), `hallNotSettled` (unsettled or at a rail), `calibrating`.

## POST /api/roof/teach/confirm

```json
{ "hallHighEnd": "open", "hallHighCounts": 2730, "hallLowCounts": 1381 }
```

Saves the three values through the settings save when they equal the device's proposal.
`200 {"success": true}`. Refusals: `noProposal`, `proposalChanged`; the settings save's own
(`409` while the roof moves or a platform action holds it, `400` if invalid, `500` if the save
failed).

## POST /api/roof/teach/discard

Drops the marks and the proposal. Always `200 {"success": true}`.
