# Data Model: Setup Wizard Journey

No device setting, route or document changes. The wizard's own model:

## Part (resume point)

`setup.step`, 0 to 11, unchanged on the device. Its meaning in the UI:

| step | Part id | First screen |
|---|---|---|
| 0 | network | name |
| 1 | board | board |
| 2 | wiring | wiring |
| 3 | testBox | testBox |
| 4 | sqmeter | sqmeter |
| 5 | park | parkSwitches |
| 6 | direction | direction |
| 7 | teach | teach |
| 8 | calibrate | calibrate |
| 9 | alerts | alerts |
| 10 | password | password |
| 11 | review | review |

Rules:

- Continue saves the screen with `step` = the part of the next screen that shows.
- Skip for now records `step` = the part of the next screen (no other change).
- Opening without an explicit start resumes at `step` in every setup state.
- An explicit start (`?screen=<id>`, `?step=<n>` from System) applies once; the address is then
  replaced with `/wizard`.

## Screen

| Screen | Part | Shows when | Answered when |
|---|---|---|---|
| name | network | always | a name |
| network | network | the board has Ethernet | always |
| board | board | always | a board chosen |
| wiring | wiring | always | always |
| testBox | testBox | always | a choice |
| sqmeter | sqmeter | always | always |
| site | sqmeter | location or time not from SQMeter | always |
| daylight | sqmeter | always | a choice |
| parkSwitches | park | always (spec 024's question replaces it) | a choice |
| nina | park | the park is not bypassed | always |
| direction | direction | always | "Yes, it opened" |
| teach | teach | always | always |
| calibrate | calibrate | always | always |
| motion | calibrate | only when opened by "Enter manually instead" | always |
| alerts | alerts | always | a choice (a channel or none) |
| password | password | always | a choice; with "Set a password", a password |
| review | review | always | always |

"Choice made" (`chosen`): picked in this visit, or the screen is before the resume point at
which the wizard opened, or setup is not `new`.

## Removed page-only state

`hallNotFitted`, `alertPick`, `offerPassword` (and the password screen's mount-time change).
