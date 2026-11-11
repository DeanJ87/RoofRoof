# Contract: the new settings

| Path | Type | Default | Range | Shown in |
|---|---|---|---|---|
| `autoClose.rearmMinutes` | integer | 10 | 5–60 | Settings → Roof → Automatic close |
| `autoClose.notStarted` | `halt` \| `retry` | `halt` | — | Automatic close |
| `autoClose.remindMinutes` | integer | 30 | 10–120 | Automatic close |
| `autoClose.remoteStop` | boolean | false | — | Automatic close |
| `autoClose.parkRequestSeconds` | integer | 30 | 10–120 | Automatic close → Advanced |
| `autoClose.parkHoldSeconds` | integer | 4 | 4–30 | Automatic close → Advanced |
| `autoClose.retryMinutes` | integer | 5 | 1–30 | Automatic close → Advanced |
| `autoClose.closeStartSeconds` | integer | 30 | 25–120 | Automatic close → Advanced |
| `autoClose.closeGuardSeconds` | integer | 30 | 10–120 | Automatic close → Advanced |
| `mqtt.changeMinSeconds` | integer | 1 | 1–60 | Automatic close → Advanced |
| `daylight.bandDeg` | number | 0.5 | 0.1–2.0 | Daylight → Advanced |
| `ui.confirmOpenClose` | boolean | false | — | Motion |

- Saved through `POST /api/config` with the existing auth and save refusals; no new route.
- Out of range: 400 with the message "<Section>: <field> must be between <min> and <max>".
- Settings dependencies: `autoClose.remoteStop` needs Alpaca on or MQTT roof commands on (D-45);
  `daylight.bandDeg` needs daylight on (D-44). The rest have none.
