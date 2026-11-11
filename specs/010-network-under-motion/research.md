# Research: Wi-Fi and OTA Under Motion

## R1. Where the check-then-act windows are

| Actor | Check | Act | Window before this spec |
|---|---|---|---|
| Roof command (web, MQTT, sequence) | `blockers()` probe | `controller.command` under the roof's lock | the probe ran before the lock: an upload or save could begin after the probe and before the move |
| Settings save, restore | `roofMoving()` | `config.save()` | a move accepted between the two overlaps the write |
| Web upload | `roofMoving()` | `FlashGuard::begin()` then the first chunk | as above, for the first chunk |
| Command-line OTA | `commandLineOtaAllowed(state())` in `onStart` | `FlashGuard::begin()` | an open between the two runs during the upload |
| Restart timer | `moving() || closeWaiting()` | `ESP.restart()` | a close cancelling the timer while its callback already runs |
| MQTT connect | `connectHold()` (`moving()`) | `connect()` | an open decided under the lock but not yet published in `stateNow` |

Decision: both sides decide under the roof's lock. Commands probe their blockers inside the lock;
platform actions check the roof's state inside the lock and register themselves (flash activity,
committed restart, connect in flight) before it is released. Whoever takes the lock first wins and
the other sees it. Alternatives: a second "platform" lock (rejected: two locks, ordering bugs); a
sequence counter re-checked after acting (rejected: the act can't be undone).

## R2. Holding the roof's lock from other tasks

The lock is taken by the control step (every 20 ms, a few hundred microseconds) and by commands.
`whileStill` only runs non-blocking actions (an atomic store, a portMUX section), so holding it
from the web task or the loop delays a step by microseconds. The esp_timer task also runs the
roof's watchdog timer; the restart callback therefore never waits for the lock (`xSemaphoreTake(…,
0)`) and retries 100 ms later. Restarting while holding the lock was rejected: the control task
would block behind it, the watchdog would trip, and the trip marker would latch a fault after the
restart.

## R3. Short writes

`FlashActivity` tracks one upload by token so a close can stop it. A settings save isn't stoppable
and must not replace the upload's token (that would stop the upload). Decision: a separate count of
short writes in progress, part of "busy" for open and close, never stale (a write always ends).
Open is refused and close deferred while it is above zero, as for uploads, but a close doesn't ask
it to stop.

## R4. A committed restart

Once the callback has checked the roof at rest, a later close would otherwise cancel the plan
(a no-op now) and start moving into the reset. Decision: `RestartPlan::commit()` under the roof's
lock; a committed plan can't be cancelled and makes `blockerDecision` refuse open and close.
Alternatives: defer the close (rejected: it would be lost in the reset all the same and spec 005
already counts a restart during a close).

## R5. The close sequence and restarts

Spec 005 keeps its stand-down and attempt time in RTC memory with a magic word and checksum, and
counts a restart during a close as a failed close. RTC memory survives a software reset, a panic
and the watchdog, but a different firmware may place the record elsewhere, and then the magic check
discards it. Decision: refuse firmware uploads and hold ordinary restarts while the sequence isn't
idle; the mandatory restart after a web UI upload (same firmware, RTC kept) only waits for the
roof. NVS was rejected: the running time changes continuously and the flash may not be written
while the roof moves.

## R6. Wi-Fi changes during the sequence

`POST /api/wifi/connect` calls `WiFi.disconnect()` and tries the new network for up to 10 s. The
park result depends on N.I.N.A. over that network; a close in progress would stop with
`parkInterlock`, and a park wait would lose polls. Decision: refuse it while the sequence is busy
and while the roof moves or a close waits. A scan doesn't disconnect but takes the radio off
channel for about 2 s; refused while the roof moves or a close waits (a poll timeout during a close
is a fault).

## R7. The setup hotspot

The hotspot is open (no password, SQMeter spec 014 heritage) and opens at boot when the saved
network isn't joined within 45 s. A request's local address tells whether it came over the hotspot
(`WiFi.softAPIP()`), as the captive-portal code already does. Decision: refuse open over the
hotspot. Opening needs a safe verdict, which needs SQMeter on the home network, so the hotspot
never had a legitimate use for open. Stop, close and clear-fault stay available: they help the roof
end closed, and the owner at the observatory with the router down may need them.

## R8. MQTT settings hand-over

As spec 004 did for the safety inputs: the web task stores the new `MQTTConfig` under a mutex; the
main loop takes it at the top of `MQTTClient::handle()`, disconnects (publishing `offline`), sets
the server and connects (subject to the roof's connect hold; held, the normal 5 s retry takes
over). `getStatus()` (web task) copies the fields under the same mutex. The connection count bumps
on the loop, so `SafetyInputs` sees the reconnect before any retained message is dispatched.

## R9. Reset reason

`esp_reset_reason()` distinguishes power-on, brown-out, software, panic, interrupt and task
watchdog, deep sleep and external reset. Logged at start-up; the status document is spec 008's.

## R10. What is already proven

| Behaviour | Test |
|---|---|
| boot between magnets: unknown, driver off | `test_roof_logic`: `test_boot_between_magnets_is_unknown_and_off` |
| stale hand-over: open stops, close faults | `test_roof_gates` (`currentGates`, `interlockLost`, `openMustStop`) |
| close wins over upload/MQTT/restart | `test_roof_gates`: blocker tests |
| restart during a close counts as failed | `test_close_restart` |
