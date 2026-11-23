# Contract: pin setup and wiring check

## `GET /api/pins` (no auth: readings and settings only, no secrets)

```json
{
  "running": {"preset": "devkit", "board": "esp32-devkit", "rpwm": 25, "lpwm": 26, "enable": 27,
              "currentR": 34, "currentL": 35, "supply": 36, "hall": 39, "parkA": 32, "parkB": 33},
  "saved": { "...": "the same shape, as saved" },
  "restartNeeded": false,
  "valid": true,
  "reason": "",
  "issues": [{"function": "lpwm", "pin": 15, "code": "strap15", "error": false, "text": "..."}],
  "psram": false,
  "presets": [{"preset": "devkit", "board": "esp32-devkit", "rpwm": 25, "...": "..."}],
  "boards": [{"board": "esp32-devkit", "ethernet": [], "header": []},
             {"board": "wt32-eth01", "ethernet": [0, 16, 18, 19, 21, 22, 23, 25, 26, 27],
              "header": [2, 4, 5, 12, 14, 15, 17, 32, 33, 35, 36, 39]}]
}
```

- `running`: the pins read at boot (what the device uses until the next restart).
- `valid`/`reason`: whether `running` passed the rules at boot; the reason names the first error,
  or says the saved setup couldn't be read.
- `issues`: the errors and warnings of `running`.
- `header: []` means every existing GPIO is available.

## `POST /api/pins/validate` (no auth; saves nothing)

Body: a pin map (the `pins` shape; a named preset alone fills in its pins). Answer 200:

```json
{"valid": false, "pins": {"...": "the map as the device read it"},
 "issues": [{"function": "rpwm", "pin": 35, "code": "inputOnly", "error": true, "text": "RPWM on GPIO35: GPIO35 is input only"}]}
```

400 with `{"error": "..."}` for a body that isn't a pin map.

## `POST /api/pins/check` (auth)

Starts the wiring check. 202 `{"started": true}`; 409 `{"error": "..."}` while the roof moves, a
close waits, the close sequence works, a calibration run or a jog is under way, the pins are
invalid, a restart is needed, or a check is running; 401 without auth.

## `GET /api/pins/check`

```json
{
  "state": "done",
  "checks": [
    {"id": "enPulldown", "level": "pass", "value": null, "unit": "", "text": "EN reads low with the internal pull-up: the pull-down holds the driver off"},
    {"id": "hall", "level": "pass", "value": 2010, "unit": "counts", "text": "..."},
    {"id": "supply", "level": "pass", "value": 12.4, "unit": "V", "text": "..."},
    {"id": "parkA", "level": "pass", "value": null, "unit": "", "text": "Closed (parked)"},
    {"id": "currentR", "level": "pass", "value": 0.2, "unit": "A", "text": "..."},
    {"id": "ethernet", "level": "pass", "value": null, "unit": "", "text": "LAN8720 found at address 1 ..."},
    {"id": "motion", "level": "skipped", "value": null, "unit": "", "text": "Direction and travel need the roof to move: use Jog and teach the ends"}
  ],
  "suggestPreset": "wt32-carrier-v1",
  "finishedAt": 123456
}
```

`state`: `idle` (never run since boot), `running`, `done`. `suggestPreset` only when the probe found
a LAN8720 on a board set up without Ethernet. `level`: `pass`, `warn`, `fail`, `skipped`.

## Settings (`GET`/`POST /api/config`)

- `pins` group (data-model.md). A save that changes it, or `wifi.withEthernet`, is refused with 409
  and the hold's text while the roof is busy; an invalid set is refused with 400 and the first
  error's text.
- `safety.rainContact` is gone; a body that still has it is accepted and the key ignored.

## Status

`ethernet` and `pins` blocks (data-model.md); `roof.rainContact` removed.
