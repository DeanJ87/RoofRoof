# Data Model: Bench Resilience

## `Safety::NinaMemory` (lib/SafetyInputLogic, ParkRule.h)

| Field | Type | Meaning |
|---|---|---|
| `lastGood` | `Nina::MountReading` | the latest reading with status Good |
| `generation` | `uint32_t` | the poller's N.I.N.A. address generation it came from |
| `known` | `bool` | `lastGood` is set |

`rememberNina(memory, reading, generation)`: a new generation, `NotConfigured` or `Waiting`
forgets; `Good` replaces; a failure keeps.

## `Safety::CloseHold`

| Field | Type | Meaning |
|---|---|---|
| `parked` | `bool` | the park result says parked |
| `riding` | `bool` | not parked only because the latest read failed, and the remembered reading confirms the park |
| `freshUntilMs` | `uint64_t` | the relied-on reading's `takenAtMs + ninaMaxAgeMs`; 0 when neither |
| `ageMs` | `uint32_t` | the relied-on reading's age at the evaluation |

## `RoofIo::GateInputs` (added)

| Field | Type | Meaning |
|---|---|---|
| `ninaGrace` | `bool` | `CloseHold::riding` |
| `parkFreshUntilMs` | `uint64_t` | `CloseHold::freshUntilMs` |
| `nowMs` | `uint64_t` | the control task's clock when checked (0 in tests and the demo: no bound) |

`closeMayContinue(in)`: `(parked || ninaGrace) && switchesClosed && (parkFreshUntilMs == 0 ? !ninaGrace : nowMs <= parkFreshUntilMs)`.

`InterlockNinaStale = 1 << 3`: the reading the move relied on is past its freshness.

## `ConfigSave` (lib/ConfigModel, ConfigSave.h)

| Item | Meaning |
|---|---|
| `Base { hasRevision, revision, values }` | read from the body's `baseRevision` and `baseValues` |
| `staleFields(values, current)` | dotted names of base leaves that differ from `current` (at most `MAX_CONFLICTS` = 16) |
| `Check { stale, conflicts }` | `checkSave(base, currentRevision, currentJson)` |
