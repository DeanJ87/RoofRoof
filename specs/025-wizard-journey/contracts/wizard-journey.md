# UI contract: the wizard journey

The stable hooks tests and docs rely on (no device API changes).

## Addresses

- `/#/wizard`: resume at `setup.step`.
- `/#/wizard?screen=<id>`: open that screen once (tests, screenshots); the address becomes
  `/#/wizard`.
- `/#/wizard?step=<n>`: open part `n - 1` once (System's "Run setup wizard" uses `step=1`).

## DOM hooks

- `[data-wizard-screen="<id>"]` on the screen's content, `[data-wizard-page="<part>"]` its part.
- Screen ids: `name`, `network`, `board`, `wiring`, `testBox`, `sqmeter`, `site`, `daylight`,
  `parkSwitches`, `nina`, `direction`, `teach`, `calibrate`, `motion`, `alerts`, `password`,
  `review`.
- The motor screens keep the Calibrate page's hooks: `[data-teach-jog]`, `[data-jog=open|close]`,
  `[data-teach-marks]`, `[data-teach-proposal]`, `[data-calibrate-checks]`, `#calibrate-run`,
  `#calibrate-result`, `[data-calibrate-confirm]`.
- `[data-choice-group="alerts"]` is a checkbox group (`role="group"`); its options are `ntfy`,
  `pushover`, `webhook`, `mqtt`, `none`.
- `[data-sqmeter-done="location|time"]`: a question SQMeter answered, with Edit.

## Leaving

Only `leaveTo` routes away (the Pins page, Settings-only fixes); it saves or records the part first.
