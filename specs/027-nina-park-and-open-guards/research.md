# Research: spec 027

## R1 Why no park request was sent

Only `CloseOnUnsafe` (the automatic close) ever called `SafetyInputs::requestPark()`. A Close from
the web UI, Alpaca or MQTT went to `RoofIo::gateCommand`, which refused it (`mountNotParked`).
`park.requestPark` was read only by the automatic close's policy. On the bench the automatic close
was in `heldByPerson` (a person's Stop), and the weather was bypassed safe, so nothing wanted the
roof closed and nothing asked. Decision: a park wait for every origin (FR-003).

## R2 Why the roof opened with the mount not parked

`gateCommand(Open)` checked the weather, daylight and the suspect switch, never the park. Decision:
Open gets the park check after the others (so a weather refusal is named first) and the interlock
re-check during an open.

## R3 N.I.N.A.'s dome sequence

Live board: `shutterstatus` 0, `canpark` false, `atpark` NotImplemented (correct for a roof). The
Close was answered with `InvalidOperation` (0x40B) "Mount not parked"; N.I.N.A. logs that and stops
the instruction. Decision: wait for the park and report closing/Slewing (the Alpaca Dome's own
model: methods return at once, the status reports progress); a failed wait reads ShutterStatus 4
(error), which N.I.N.A. treats as a failed close. Already at the end: success before any check.

## R4 https for N.I.N.A.

`SafetyPoller::httpGet` already opens `https://` with `WiFiClientSecure::setInsecure()` under the
one TLS session (`TlsLock`), for SQMeter. Only the config check refused it. Decision: allow it; the
poll and the test share that path. N.I.N.A. is polled every 2 s; with https each poll holds the TLS
session briefly (handshake about 1 s on the ESP32), so alerts may wait for it: the lock already
serialises them.

## R5 Budgets

Flash 92.8% before the work (about 40 KB to the 95% fail line). LittleFS at its 75% limit, which
counts a 64 KB language-file allowance kept since spec 001; the UI must not grow: new strings are
short and some long explanations are shortened. `Config.h` at 600 lines: the new event setting
replaces one comment line.

## R6 Version

No `v*` tag exists, and the CHANGELOG is all "Unreleased"; `include/version.h` holds `0.1.0+dev`,
the next release with "+dev". Decision: a PlatformIO pre-script appends the short commit
(`0.1.0+dev.f3c98f6`); CI's tag stamp is unchanged.

## R7 Home Assistant Alerts switch

`discoveryGroups` announced the switch only while `alerts.enabled` (the master switch, dependency
D-13). The bench device had the master off, so the switch was removed. Decision: announce it
whenever discovery is on; the dependency entry goes.

## R8 Hall rail

A rail reading latched `HallRail` whether the motor ran or not; only `clearFault` from a person
cleared it, and a restart kept it (NVS). Decision: remember whether the fault latched with the
driver off; such a fault clears after 60 s of non-rail readings with no move active. After a
restart the flag is not kept (conservative: it then waits for a person).
