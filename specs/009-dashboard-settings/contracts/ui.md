# UI contract: Roof card, Stop bar, Roof tab

What the device's web UI promises, and what the tests hold it to. Device interfaces used, unchanged:
`POST /api/roof/{open,close,stop,clear-fault}` with `X-RoofRoof: 1` (spec 003), `/ws/status` and
`GET /api/status` with the `roof` section (specs 003, 008), `GET/POST /api/config` (spec 001).

## Roof card (`data-inventory="roof-card"`, card id `roof`)

| Element | Contract |
|---|---|
| Title | "Roof", with a "?" hint; the state pill in the card's actions |
| Position | a meter, "N % open" or "Unknown" |
| Buttons | Open, Close, Stop (`danger`). Stop has no `disabled` attribute ever. A disabled Open or Close has `aria-describedby` pointing at the Note with its reason |
| Notes | one line each: the reasons, position unknown, automatic close under way, close waiting, the last command result |
| Fault | the fault's text in a bad Note and a Clear fault button, only while a fault is latched |
| Readings | Weather (pill, "?"), Park (pill, "?"), Automatic close (pill, "?", when not idle), Supply, Motor current (current sense on), Park switches (fitted), Rain contact (fitted) |
| Hall | the scale image with its text alternative, and the level |

## Stop bar (`.stop-bar`)

Shown on every page while the last status says opening, closing or a close is waiting. One `danger`
button, "Stop roof", never disabled. Not inside `.app-header`, not before the dashboard's grid.

## Status card rows

`roof-fault` and `roof-close`, as in [data-model.md](../data-model.md#status-card-items-glancets).

## Roof tab (`?tab=roof`)

Cards, in order: Motion (`#roof`), Hall sensor (`#hall`), Current and supply (`#current`), Weather
sources (`#safety`), Park (`#park`). Every field has `data-field` equal to its config path.
`?section=roof|safety|park` opens the tab at that card.

## Commands

| Answer | Shown |
|---|---|
| 200 | nothing new (the state follows on the live data) |
| 202 `pending` | muted note with the device's `message` |
| 409 | bad note with the device's `error`; `restarting` and `hotspot` also disable as in the data model |
| 401, 403, other | bad note with the device's `error`, or "The device refused the command" |
| no answer | bad note "Couldn't reach the device" (for Stop: "Stop not confirmed: couldn't reach the device") |
