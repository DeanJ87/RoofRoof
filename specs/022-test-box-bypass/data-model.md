# Data Model: Test Box Bypass

## TestBoxConfig (`Config::testBox`, persisted with the settings)

| Field | JSON | Type | Default | Rule |
|---|---|---|---|---|
| park | `testBox.park` | bool | false | refused with a N.I.N.A. address set |
| weather | `testBox.weather` | bool | false | refused with SQMeter HTTP or MQTT on |
| daylight | `testBox.daylight` | bool | false | |
| hours | `testBox.hours` | integer | 72 | 1-720 |
| until | `testBox.until` | integer (UTC s) | 0 | device-owned: never taken from a request or a restore |

Settings saved before this spec have no `testBox`: every bypass off (a migration never turns on a
relaxation).

### The save stamp (`ConfigSave::stampTestBox`)

Inputs: the live settings, the merged settings, whether the body sent `testBox.hours`, the clock.

1. No bypass set after the merge → `until = 0`.
2. A bypass set after the merge that was not set before, or `hours` sent → needs the clock (else
   refused: "Test box: the clock isn't set yet ..."); `until = now + hours × 3600`.
3. Otherwise `until` keeps the live value.

## TestBox::State (derived each evaluation, never stored)

| Field | Meaning |
|---|---|
| park, weather, daylight | that bypass is set and in effect now |
| set | any bypass is set |
| expired | set, the clock set, and now ≥ until (or until = 0) |
| waitingForClock | set and the clock not set |
| until | the deadline |
| parkRequests | `park.requestPark` on and a N.I.N.A. address set |

In effect = set ∧ clock set ∧ now < until; park also ∧ no N.I.N.A. address; weather also ∧ both
SQMeter sources off.

`TestBox::bits(state)`: 1 park, 2 weather, 4 daylight.

## Effects

- **Weather** (`WeatherVerdict`): with `weather` in effect and `reasons == ReasonNoWeatherSource` →
  `verdict = Safe`, `reasons = 0`, `bypassed = true`. Otherwise unchanged.
- **Park** (`ParkResult`, `CloseHold`): with `park` in effect and N.I.N.A. not configured → the
  N.I.N.A. reasons dropped; if none left: `parked = true`, `bypassed = true`; the hold is
  `{parked: true, riding: false, freshUntilMs: 0}`. Switch reasons keep it not parked.
- **Daylight** (`Daylight::View`): `bypassed = daylight in effect`; `closes()` false while bypassed.

## Status object `roof.testBox` (see contracts/test-box.md)

`set`, `active`, `park`, `weather`, `daylight`, `until`, `expired`, `waitingForClock`, `text`.

## Alert event `test_box`

`AlertsConfig::testBox` (level 1-4, default 3; 0 refused on save, raised to 1 on load).
`Alerts::RoofInputs::testBox` (bits) and `testBoxText`. The engine sends when a bit appears that it
has not seen since boot.

## ParkConfig::requestPark (`park.requestPark`)

bool, default true (also after an upgrade). The automatic close sends N.I.N.A. a park request only
while it is on; off, it only waits for the park. Shown on the Mount park card and the wizard's test
box screen; `roof.testBox.parkRequests` and the banner say when it would be sent.
