# Data Model: Setup Wizard Redesign

No device data changes. `setup.state` and `setup.step` (a page index, 0 to 10) are spec 015's.

## Screen (web, `components/wizard/screens.ts`)

| Field | Type | Meaning |
|---|---|---|
| `id` | `ScreenId` | stable id, used in `?screen=` and `data-wizard-screen` |
| `page` | `WizardPageId` | spec 015's page: the save records its index as `setup.step` |
| `question` | i18n key | the bubble's question, the dialog's name |
| `why` | i18n key | the "Why this matters" text |
| `shows?` | `(ctx) => boolean` | false leaves the screen out (and out of the progress count) |
| `answered?` | `(ctx) => boolean` | false makes Continue unavailable; default true |
| `missing?` | i18n key | what the note under an unavailable Continue says |
| `skip?` | boolean | Skip for now offered; default true (false on the review) |
| `Body` | component | the screen's content |

`ctx` is the page props (config as edited, status, the wizard's own state).

## Wizard state (component state, not saved)

| Field | Meaning |
|---|---|
| `index` | the current screen's position in the full list |
| `picked` | set of choice screens answered in this visit (selection shows) |
| `seenUpTo` | the first screen of the page opened at; choice screens before it show the current setting |
| `hallNotFitted` | spec 015's, unchanged |
| `leaving` | the "Leave setup?" screen is open |
| `skipping` | the skip warning is shown on it |
| `finished` | the done screen is shown |

## Selection rule

A choice screen shows the current setting as selected when the setup state isn't `new`, the
screen is before `seenUpTo`, or it is in `picked`; otherwise nothing is selected. The password
screen also selects "on" when spec 015's offer applies.
