# Quickstart: Validating Close on Unsafe

## 1. Everything CI runs

Run spec 001's [quickstart §1](../001-platform-bootstrap/quickstart.md) in full. New native suites:

```bash
~/.platformio/penv/bin/pio test -e native -f test_close_sequence -f test_close_standdown -f test_close_text -f test_close_properties
```

Expected: all pass; zero warnings; the flash budget check passes.

## 2. Settings round trip (any board)

1. `POST /api/config` with `{"park":{"timeoutSeconds":20}}`: rejected with the range message.
2. `{"park":{"timeoutSeconds":120}}`: accepted, and still there after a restart.
3. A config saved before this spec loads with `park.timeoutSeconds` 240.

## 3. Bench checks (real board, motor disconnected, log at 115200 baud, `Close` tag)

Make the weather unsafe by turning off SQMeter's safe output (or with SQMeter's manual override).

| Step | Expected |
|---|---|
| Roof open (magnet), N.I.N.A. connected, mount unparked; make it unsafe | `Close sequence started` and a park request within 1 s; N.I.N.A. parks the mount |
| Mount reports parked (and the park switches close) | `close` commanded 4-5 s later; the roof closes; `Roof closed after unsafe weather` |
| Repeat with N.I.N.A. closed | after `park.timeoutSeconds`, a wake-level alert "Mount didn't park"; the roof stays open; no park request for 5 min |
| Repeat; press stop (REST) while closing | the roof stops; alert "Automatic close stopped"; nothing more while unsafe |
| Restart with the roof between the magnets, unsafe | no park request; alert "Roof can't close by itself: position unknown" after 2 s |
| Flip the verdict safe/unsafe every few seconds | park requests at most every 30 s; with N.I.N.A. closed, "Mount didn't park" still arrives within `park.timeoutSeconds` |
| Unsafe, mount parked: press stop (REST) in the 4 s before the close starts | no close starts; alert "Automatic close stopped" (the roof refuses the automatic close after a person's stop) |
| Stand the sequence down with stop, then restart the device from the Updates page | after the restart: `still stood down`, "Still: Automatic close stopped" alert, no close |
| No alert channel set up | set one up (ntfy, Pushover, webhook or MQTT) before the roof runs unattended; the master switch doesn't hold these alerts back |
