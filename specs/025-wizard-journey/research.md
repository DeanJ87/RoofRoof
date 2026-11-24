# Research: Setup Wizard Journey

## R1. The password step reopening (root cause)

Reproduced in the demo (spec 023's wizard):

1. `PasswordScreen` ran a layout effect on mount: when `offerPassword()` said so (once per mount
   of the wizard, and only while no password was saved), it set `auth.enabled` to true and marked
   the screen chosen. After "No password" was saved (protection off, no password: the factory
   state), every later visit to that screen from a fresh mount of the wizard (a reload, leaving for
   the Calibrate page or Settings and coming back) turned protection back on, selected "Set a
   password" and disabled Continue until a password was typed.
2. `startScreen` ignored `setup.step` unless the state was `new` or `offered`. On a re-run from
   System (state `done`, address `/wizard?step=1`) the explicit `?step=1` stayed in the address, so
   a reload or a browser Back from the Calibrate page or Settings opened the wizard at its first
   part again: the name, then the password screen with the choice undone (1).
3. Skip for now never recorded the part, so a reload after skipping went back to the last part
   that was continued.

**Decision**: no screen changes a setting when it opens (the password screen asks with nothing
chosen, "Set a password" recommended); resume reads `setup.step` in every state when the address
gives no explicit start; an explicit `?screen=`/`?step=` is consumed (the address is replaced);
Skip for now records the next part. Tests: a Vitest test that saves "No password", remounts the
wizard and finds "No password" still chosen with protection off (fails on the old code), and the
resume tests per state; Playwright reloads mid-wizard on both branches.

**Alternatives considered**: a new per-screen "answered" record on the device (firmware, NVS and
contract change, against the size budget and out of scope); browser storage (per browser, not per
device; lost on another phone).

## R2. The same kind of bug elsewhere (audit)

| Screen | Page-only state or mount-time change | Fix |
|---|---|---|
| password | auto-offer on mount | removed (R1) |
| hallFitted | "not fitted yet" kept in the page; lost on reload, the review then said OK | question removed; the review reads the live Hall level |
| alertChannel | the picked channel kept in the page; a remount showed another channel | multiple choice read from the settings |
| board | suggests the detected board on a new device | kept: it only fills an unsaved form, never re-fires once a board is saved |
| any choice screen | `chosen` from the resume point at mount | kept: screens before the resume point show their saved answer |
| leave links (Teach, calibration, Custom pins, "Turn on MQTT", wiring check's Teach link) | edits dropped, resume at the part's start | inline, or save first then resume (R4) |

## R3. Reusing the Calibrate page's components in the dialog

`TeachCard`, `StartForm`, `Running` and `Result` are self-contained (their own attended toggle,
checks, API calls, status hooks). They render a `SettingsCard`; a `plain` form renders the same
body in a `card-group` so the dialog shows no card inside a card. The Calibrate page's `accept`
(save through the settings save, a spec 021 conflict rebased and reported) moves into a shared
function used by both. After a teach confirmation or an accepted run the wizard reloads its
settings form, as the page does. Stop: the dialog's Stop bar (constitution VII) shows while the
roof moves, the run's own Stop always.

## R4. Leaving the wizard

Only two destinations remain: the Pins page (Advanced analog, which the wizard doesn't ask) and a
Settings-only dependency fix (e.g. "Broker not connected" → MQTT settings). Both go through one
`leaveTo(url)`: save the screen when dirty (with the current part as `setup.step`), else record
the part, then route. Fixes the wizard can make itself are applied in place: `mqtt-off`,
`alerts-off`, `alpaca-off` turn the setting on; `location-unknown`, `no-sqmeter-address`,
`clock-not-set` go to the SQMeter or site screen.

## R5. Alerts as a multiple choice

`ChoiceGroup` gains a checkbox form (`multiple`, values as a list, a `group` role). The screen
maps ticks to `alerts.<channel>.enabled` (and `alerts.enabled` on when any is ticked); "No alerts
for now" clears every channel. Each ticked channel shows its Settings group (`CHANNEL_GROUPS`)
without its own on switch; MQTT ticked while MQTT is off also turns MQTT on and shows its broker
fields (`MqttFields`). The wizard's refused-save note lists the failing fields' messages that are
not on the screen, and says "Fix the fields marked below" when they all are.

## R6. Size

Baseline: App chunk 141.4 KB gzipped (148 KB on LittleFS), total 332 of 336 KB. Removed: seven
screens and their strings (Hall fitted, Hall typed, supply, the two link screens, weather source and
address, alert channel and details, Alpaca), the weather Test result, the password auto-offer.
Added: one motor file reusing existing components, the checkbox form, a few strings. Measured
after implementation with `tools/ui/size_check.py`.
