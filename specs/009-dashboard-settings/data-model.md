# Data Model: Dashboard and Settings

All types are web-only (`web/src/lib/roofView.ts`, `web/src/hooks/useRoofCommand.ts`). The inputs
are the status document's `roof` section and `wifi.apMode` (`web/src/types/status.ts`, contract:
spec 008) and the `roof`, `safety` and `park` settings (`web/src/types/config.ts`).

## LiveState

| Field | Type | Meaning |
|---|---|---|
| `status` | `SystemStatus \| null` | the last `/ws/status` message |
| `connected` | boolean | the socket is open |
| `quiet` | boolean | no message for more than 5 s |
| `reconnectedSince` | number \| null | when a message last arrived after the socket was lost (ms) |

`live = status !== null && connected && !quiet`.

## CommandResult

| Field | Type | Meaning |
|---|---|---|
| `command` | `'open' \| 'close' \| 'stop' \| 'clear-fault'` | what was sent |
| `outcome` | `'accepted' \| 'pending' \| 'refused' \| 'unreachable' \| 'error'` | 200, 202, 409, no answer, anything else |
| `reason` | string | the device's `reason` (409) or empty |
| `text` | string | the device's `error` or `message`, translated when known |
| `at` | number | when it was answered (ms) |
| `stateAt` | `RoofState \| null` | the roof's state when it was answered |

`stateAt` is taken when the answer arrives. Expiry: shown until the roof's state differs from
`stateAt`, or `RESULT_MS` (15 s) pass; a Stop that wasn't accepted stays until the roof is seen not
opening or closing. Commands time out after 5 s (unreachable). A
`restarting` refusal holds Open and Close until a message arrives after a reconnect later than `at`,
or `RESTART_HOLD_MS` (30 s) pass.

## ButtonView

| Field | Type | Meaning |
|---|---|---|
| `disabled` | boolean | always false for Stop |
| `reason` | string | why it's disabled, shown in a Note tied to the button (`aria-describedby`) |
| `busy` | boolean | a command is in flight (Open and Close only) |

Open's reasons, first match wins: no live data → roof not reporting → restarting → fault latched →
in flight → open / opening → weather not safe (`weatherText`) → hotspot → supply low or unreadable →
Hall not settled.

Close's reasons, first match wins: no live data → roof not reporting → restarting → fault latched →
in flight → closed / closing → close waiting → park not confirmed (`parkText`) → supply low or
unreadable → Hall not settled.

## RoofView

| Field | Type | Meaning |
|---|---|---|
| `open`, `close` | `ButtonView` | |
| `stop` | `{ disabled: false }` | by construction |
| `statePill` | `{ label, tone }` | Closed (green), Open (amber), Opening/Closing (amber), Stopped (amber), Unknown (dim), Error (red) |
| `position` | `{ percent: number \| null, text }` | `null` and "Unknown" while unknown |
| `notes` | `{ id, tone, text }[]` | position unknown (muted), automatic close under way (warn), close waiting (muted), the last command result |
| `weather` | `{ label, tone, text }` | Safe / Unsafe / Unknown with `weatherText` |
| `park` | `{ label, tone, text }` | Parked / Not parked with `parkText` |
| `closePhase` | `{ label, tone, text } \| null` | null while `idle` |

## stopBarShown(roof)

`roof.state` is `opening` or `closing`, or `roof.closePending`, from the last status, whether or not
it is still fresh; or no status has arrived for 5 s. The bar's height is published as `--stop-bar`
so the save bar and toasts sit above it.

## HallScale

`{ counts, level, low, high }` → positions as fractions of 4095, clamped; thresholds omitted when
unknown.

## Status card items (glance.ts)

| id | Severity | When | Pill | "?" |
|---|---|---|---|---|
| `roof-fault` | problem | `roof.fault` set | Fault | the fault's text |
| `roof-close` | note | `closePhase` `heldByPerson` | Held | the phase's explanation |
| `roof-close` | problem | `closePhase` `cannotMove` or `halted` | Can't move / Halted | the phase's explanation |

## Settings

The Roof tab edits `roof.*` (spec 002, `roofSchema.ts`), `safety.*` (spec 004) and `park.*`
(specs 004, 005) with the existing schemas and the shared range fixture. Error paths under `roof`,
`safety` and `park` belong to the Roof tab; `?section=roof|safety|park` opens it.
