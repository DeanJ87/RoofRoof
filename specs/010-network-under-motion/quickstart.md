# Quickstart: Wi-Fi and OTA Under Motion

## 1. Local checks

As spec 001's quickstart §1, plus:

```bash
~/.platformio/penv/bin/pio test -e native -f test_platform_hold -f test_roof_gates
```

## 2. Deliberate breakage (by hand, results in the PR)

Remove each rule in turn and confirm a test fails: the `Moving` case, the `CloseWaiting` case, the
`CloseSequence` case for each action, the mandatory restart exemption, the short-write count in
open and close, the committed restart in open and close, the hotspot rule.

## 3. Bench (board, motor disconnected or bench mode)

1. Script 200 rounds of `PUT /api/config` (unchanged settings) and `POST /api/roof/close` /
   `stop` sent together; `maxStepGapMs` stays under 100 and `watchdogTrips` at 0.
2. Make the weather unsafe with the roof open and N.I.N.A. not parking: `POST /api/restart`,
   a firmware upload and `POST /api/wifi/connect` answer 409 naming the automatic close.
3. Open the setup hotspot (saved network off); from a phone on it `POST /api/roof/open` answers 409
   `hotspot`; stop and close behave as from the home network.
4. With the broker unreachable, save MQTT settings: the reply comes within 1 s; the log shows the
   reconnect attempt from the main loop.
5. Pull the 5 V supply mid-move: after power returns the log names the reset reason, the roof reads
   unknown between the magnets and doesn't move.
