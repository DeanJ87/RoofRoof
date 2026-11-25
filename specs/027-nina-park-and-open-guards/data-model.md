# Data model: spec 027

## Override bits (`RoofIo::OverrideBit`)

| Bit | Name | For | Who |
|---|---|---|---|
| 1 | `park` | one Close | person |
| 2 | `weather` | Open, while SQMeter alone says unsafe (no rain, no doubt) | person |
| 4 | `daylight` | Open | person |

Request names: `"mountNotParked"`, `"weatherNotSafe"`, `"daylight"` (the gate refusal names).

## Override in effect (`roof.override`)

Set when a person's overridden Open is accepted (weather and daylight bits). Ends (bits cleared)
when: the roof reads Closed; a Close is accepted; none of its reasons is present now; a refusal not
covered appears (rain, doubt, park, switch check). A Close override lasts for that close only.

## Park wait (`RoofIo::ParkWait`)

| Field | Meaning |
|---|---|
| command | Open or Close |
| origin | who asked |
| startedMs, limitMs | `park.timeoutSeconds` |
| lastRequestMs, gapMs | `autoClose.parkRequestSeconds` |
| holdMs, parkedSinceMs | `autoClose.parkHoldSeconds` |
| overrides | the person's bits carried to the run |
| failed, failText | the last wait's end when it failed (Alpaca error until the next command) |

Transitions: Idle → Waiting (start) → Run (park held for holdMs) → Idle; Waiting → Idle with
`GaveUp` (limit, critical alert), `Cancelled` (stop, new command, automatic close for Open) or
`Refused` (another check).

## Status additions (`/api/status` `roof`, the MQTT state document)

- `parkWait`: `null` or `{command, origin, leftSeconds, asked}`; `parkWaitEnded`: why the last wait
  gave up or was refused, or null.
- `override`: array of names in effect; `openOverridable`: names a person may override now.
- `nina`: `{set, state, reachable, connected, atPark, ageSeconds, text}`.
- `sqmeter`: `{on, state, lastReplySeconds, text}` (`lastReplySeconds` null without a good reply).

## Alert event `nina_lost`

Default level 2 (warning), settle 60 s, variables `{reason}`. Title "N.I.N.A. not reachable".
