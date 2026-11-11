# Contract: Roof Hardware

What this spec offers the specs that build on it (005 close on unsafe, 006 Alpaca Dome, 008 MQTT
and Home Assistant, 009 dashboard, 011 demo). Types are in [data-model.md](../data-model.md).

## REST (all `requireAuth`)

| Route | Accepted | Refused |
|---|---|---|
| `POST /api/roof/open` | 200 `{"success":true,"state":"opening"}` | 409 `{"success":false,"reason":"weatherNotSafe","error":"Weather not safe: ..."}` or a motion refusal |
| `POST /api/roof/close` | 200 `{"success":true,"state":"closing"}` | 409 `reason` `mountNotParked` or a motion refusal |
| `POST /api/roof/stop` | 200 always (after auth) | - |
| `POST /api/roof/clear-fault` | 200 `{"success":true,"state":...}` | - (REST is a person) |
| `POST/PUT /api/config`, `POST /api/backup/apply` | as before | 409 `{"error":"The roof is moving: roof and park settings can't change until it stops"}` when `roof.*` or `park.*` changes while opening or closing |

`reason` is `weatherNotSafe`, `mountNotParked` or a `Roof::refusalName` (`faultLatched`,
`hallNotSettled`, `positionUnknown`, `supplyLow`, `supplyUnreadable`, `currentUnreadable`,
`settingsInvalid`). `error` states the measured value and the limit or the failed input.
REST commands use `Roof::Origin::Person`.

## Status

`/api/status` and `/ws/status` gain `roof` (fields in the data model). A state or fault change sends
a live update within 250 ms.

## Firmware API: `SQM::RoofControl` (`include/RoofControl.h`)

```cpp
// setup(), after the config is loaded and before the web server starts. Creates the controller,
// latches a saved fault, starts the PWM, the control task and the watchdog.
bool begin(const Config &config);

// Any task. Gated (weather for open, park for close), then Roof::Controller::command, then the
// output is applied. Stop is never refused.
Outcome command(Roof::Command command, Roof::Origin origin);
Outcome clearFault(Roof::Origin origin);

// Any task. Latches an External fault with this cause (spec 005 may add causes).
void raiseFault(RoofIo::FaultCause cause, float measured, float limit);

// Settings: ask before saving; then hand the saved config over.
bool settingsSaveRefused(const Config &next) const;
void updateConfig(const Config &config);

// Main loop, after SafetyInputs::handle(): the weather verdict and park result for the gates.
void setSafety(const Safety::WeatherVerdict &weather, const Safety::ParkResult &park);
// Main loop: the switch and contact readings for SafetyInputs.
Safety::SwitchReading parkSwitch(uint8_t index) const; // 0 or 1
Safety::RainContact rainContact() const;

// Any task: a copy of everything the status section shows; revision() bumps on a state or fault change.
RoofIo::Snapshot snapshot() const;
uint32_t revision() const;
```

`Outcome` is `{accepted, reason (machine name), text, state}`.

## Pure API: `lib/RoofIoLogic`

| Header | Provides |
|---|---|
| `RoofReadings.h` | the fixed values, `medianCounts`, `averageMillivolts`, `supplyVolts`, `motorAmps` |
| `SwitchInput.h` | `Contact`, `SwitchInput`, `parkReading`, `rainReading`, `contactName` |
| `StepWatchdog.h` | `watchdogExpired`, `stepGapMs` |
| `FaultRecord.h` | `FaultCause`, `FaultRecord`, `encodeFault`, `decodeFault`, `causeName` |
| `RoofGates.h` | `GateInputs`, `GateRefusal`, `switchesClosed`, `parkHolds`, `gateCommand`, `interlockLost`, `interlockBits`, `openMustStop`, `gateRefusalName` |
| `SettingsGuard.h` | `motionSettingsChanged`, `settingsSaveRefused` |
| `RoofText.h` | `describeFault`, `describeRefusal`, `describeGate` |
| `RoofStatus.h` | `Snapshot`, `writeStatus` |

## Guarantees (each proven by a native test, or on the bench where marked)

1. A close is accepted only when the park result is parked and every fitted switch reads closed;
   losing either during a close latches `parkInterlock` on that step.
2. An open is accepted only when the verdict is `Safe`; a verdict that is not safe during an open
   stops it.
3. The watchdog trips when the gap exceeds 100 ms, never at 100 ms, across the 32-bit wrap; after a
   trip EN can't be set high until the fault is latched (bench: SC-002).
4. A stored fault record round-trips for every kind and cause; an empty store is no fault; every
   damaged record (bad magic, version, kind, cause or checksum, wrong length) is a fault.
5. A switch input is unreadable until three equal readings and after a second of chatter.
6. A save changing `roof.*` or `park.*` is refused while opening or closing and allowed otherwise.
7. Every fault and refusal text names the measured value and the limit or the failed input.
