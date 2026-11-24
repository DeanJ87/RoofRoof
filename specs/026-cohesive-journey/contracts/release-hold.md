# Contract: release the automatic close's hold, and its countdown

## `POST /api/roof/release-hold`

Login (when on) and the header `X-RoofRoof: 1`, like every roof command.

Body: `{ "attended": true }`.

| Answer | When |
|---|---|
| `200 {"success": true}` | The automatic close stands down (`heldByPerson` or `halted`) and nothing wants the roof closed (weather safe, no daylight close). The next step of the sequence (within 100 ms) checks again and ends the stand-down (`EndedSafe`). |
| `400 {"error": "Confirm that you are at the roof and can see it"}` | `attended` missing or not `true`. |
| `409 {"error": "The automatic close isn't standing down: there is nothing to release"}` | Any other phase. |
| `409 {"error": "The weather or the daylight check wants the roof closed: the stand-down can't be released now"}` | Not safe, or daylight wants a close. |
| `401` / `403` | Login, header. |

The release never moves the roof. Afterwards unsafe weather starts a new attempt at once (as after
the re-arm spell). A person's new Stop on the same step wins. `cannotMove` is never released.
The decision lives in `lib/CloseSequenceLogic` (`Inputs::releaseHold`, `releaseRefusal`).

## `roof.closeRearmSeconds` (status document only)

Integer seconds until a stand-down ends by itself if nothing wants the roof closed meanwhile:
`max(safe since, stood down since) + rearm spell - now`. `null` while the sequence isn't stood down,
or while something wants the roof closed (the count starts again once it doesn't). Not in the MQTT
state document.
