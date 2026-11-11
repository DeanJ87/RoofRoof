# Data Model: Daylight Failsafe

## Settings (`lib/ConfigModel`)

| Key | Type | Default | Range | Notes |
|---|---|---|---|---|
| `daylight.mode` | `"off"` \| `"alert"` \| `"close"` | `"alert"` | | `close` is "Alert and close". An older config loads as `alert`. |
| `daylight.sunAltitudeDeg` | number | `0` | -18 to 30 | The sun's centre, degrees above the horizon. |
| `location.set` | bool | `false` | | SQMeter's name. |
| `location.latitude` | number | `0` | -90 to 90 | Degrees, north positive. |
| `location.longitude` | number | `0` | -180 to 180 | Degrees, east positive. |
| `alerts.events.daylight_open` | event setting | level 3 | | non-critical |
| `alerts.events.daylight_closed` | event setting | level 2 | | non-critical |
| `alerts.events.daylight_unknown` | event setting | level 2 | | non-critical |

## `lib/DaylightLogic`

```cpp
namespace SQM::Daylight {
  enum class Mode : uint8_t { Off, Alert, Close };
  enum class State : uint8_t { Off, Unknown, Night, Day };
  enum class Missing : uint8_t { None, Location, Clock };
  constexpr float DEFAULT_LIMIT_DEG = 0.0f, MIN_LIMIT_DEG = -18.0f, MAX_LIMIT_DEG = 30.0f;
  constexpr double BAND_DEG = 0.5;          // back to night only this far below the limit
  constexpr double FORCED_ABOVE_DEG = 10.0; // the demo's "Sun up": limit + this
  struct Settings { Mode mode; float limitDeg; bool locationSet; double latitude, longitude; };
  struct View {
    Mode mode; State state; Missing missing;
    double altitudeDeg; // NaN unless the location and clock are known (or forced)
    float limitDeg;
    bool closes() const; // mode Close and state Day
  };
  class Watch { View update(const Settings&, bool clockSet, int64_t epochSeconds, bool forceSunUp); };
  std::string describe(const View&); // "Sun 3.2° ≥ 0.0°", "Sun -4.1° < 0.0°", "Location not set", "Clock not set", "Off"
  std::string actionText(Mode mode, bool closing);
  const char *modeName(Mode); const char *stateName(State);
  Settings settingsFrom(const Config&);
}
```

State transitions of `Watch::update` (once a second):

| Condition (in order) | State |
|---|---|
| mode or threshold changed since the last pass | band reset, then the rows below |
| mode Off | Off (band reset) |
| forced | Day, altitude = limit + 10° |
| location not set | Unknown / Location (band reset) |
| clock not set | Unknown / Clock (band reset) |
| altitude ≥ limit | Day |
| was Day and altitude ≥ limit - 0.5° | Day |
| otherwise | Night |

## Close sequence (`lib/CloseSequenceLogic`)

- `Inputs::daylight` (bool, default false): daylight wants the roof closed (`View::closes()`).
- `Decision::daylight` (bool): the decision belongs to a daylight-only episode.
- `Sequence::daylightEpisode()`: the current episode is daylight-only.
- `Sequence::Saved::daylight` (bool): kept across a restart (RTC flag bit 1).

Per step: `wanted = !weatherSafe || daylight`; the spec 005 table runs with "weather safe"
read as `!wanted`. Before it:

| Phase | Condition | Effect |
|---|---|---|
| Idle | | episode daylight-only = `daylight && weatherSafe` |
| any other | weather not safe, episode daylight-only, no person's stop this step, phase RetryWait or CannotMove, or HeldByPerson or Halted with the weather not safe for `WEATHER_REARM_CONFIRM_MS` (30 s; a stop restarts it) | enter Idle (no event); episode weather; critical alert throttle cleared; the table then starts the weather attempt |
| any other | weather not safe (HeldByPerson: after the 30 s) | episode weather |

## Alert conditions (`Alerts::RoofInputs` additions)

| Field | Meaning |
|---|---|
| `daylightOpen` | mode on, state Day, roof not closed, no weather episode running |
| `daylightSun` | `describe(view)` |
| `daylightAction` | `actionText(mode, closing)` |
| `daylightUnknown` | mode on, state Unknown |
| `daylightMissing` | "Location not set" / "Clock not set" |
| `daylightClosedCount` | daylight episodes that ended closed, since start |
| `daylightClosedSun` | the sun's text during that episode |

Trackers: `daylightOpen` settle 30 s; `daylightUnknown` settle 600 s; `daylightClosed` like
`roofClosed`. Announced bits: 4 daylight open, 5 daylight unknown.

## Gates (`lib/RoofIoLogic`)

`GateInputs::daylightCloses`, `GateInputs::daylightText`; `GateRefusal::Daylight` named
`daylight`; `openStopRefusal(gates)` says which refusal stopped an open.

## Status document (`roof.daylight`)

```json
{ "mode": "close", "state": "day", "sunAltitudeDeg": 3.2, "limitDeg": 0, "text": "Sun 3.2° ≥ 0.0°", "closes": true }
```

`sunAltitudeDeg` is null when the state is off or unknown.

A daylight-only HeldByPerson raises no 30-minute reminders while the weather is safe. A weather episode whose weather has been safe for `REARM_SAFE_MS` while daylight still wants a close becomes daylight-only. A step adds at most `MAX_COUNT_STEP_MS` (1 s) to the 30 s count.
