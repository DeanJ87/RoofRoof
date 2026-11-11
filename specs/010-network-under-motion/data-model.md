# Data Model: Wi-Fi and OTA Under Motion

## RoofIo::PlatformAction

| Value | Used for |
|---|---|
| `FlashWrite` | settings save, restore without restart, Wi-Fi credentials after a join, alerts pause state, web UI upload |
| `Restart` | a person's restart, a restore's restart, the restart after a firmware upload, the restart after joining from the hotspot |
| `MandatoryRestart` | the restart after a web UI upload |
| `FirmwareUpload` | `POST /api/update`, command-line OTA |
| `WifiChange` | `POST /api/wifi/connect` |
| `WifiScan` | `GET /api/wifi/scan` |

## RoofIo::HoldReason

`None`, `Moving`, `CloseWaiting`, `CloseSequence`. Names (for logs and tests) `none`, `moving`,
`closeWaiting`, `closeSequence`.

## RoofIo::PlatformState

| Field | Meaning |
|---|---|
| `roof` | `Roof::State` |
| `closeWaiting` | spec 003's `closePending` |
| `sequenceBusy` | spec 005's sequence is in any phase but `Idle` |

## RoofIo::FlashActivity (extended)

Adds `shortWrites` (count): `holdShort()`, `releaseShort()` (never below 0), `writing()`.

## RoofIo::RestartPlan (extended)

Adds `committed`: `commit()` (true if a restart was due; then `cancel()` returns false and
`cancellable()` is false), `committed()`, `mandatory()` (the due restart can't be cancelled).
`clear()` resets everything.

## RoofIo::Blockers (extended)

| Field | Effect |
|---|---|
| `flashWriting` | a short write: open refused, close deferred (no stop request) |
| `restartCommitted` | open and close refused ("The device is restarting") |
