# Data Model: Setup Wizard

## Settings (`lib/ConfigModel`)

| Key | Type | Default | Notes |
|---|---|---|---|
| `setup.state` | `"new"` \| `"offered"` \| `"done"` | `"new"` | Stored settings without a `setup` object load as `"offered"`. Any other value is refused. |
| `setup.step` | integer | `0` | 0-9: the page to resume at. |

```cpp
struct SetupConfig {
  enum class State : uint8_t { New = 0, Offered = 1, Done = 2 };
  State state = State::New;
  uint8_t step = 0;
  static constexpr uint8_t STEP_COUNT = 10;
};
// Config: SetupConfig setup;
// Settings stored by an earlier release (no "setup" object): offered, step 0.
static bool storedBeforeSetup(const std::string &storedJson);
void markStoredBeforeSetup();
```

Not a motion setting, not a restart setting, not in the settings-dependency catalogue (it
depends on nothing).

## Inputs report (`lib/SafetyInputLogic`)

```cpp
namespace SQM::Safety {
  struct InputsReport {
    WeatherVerdict weather;         // with http and mqtt SourceStatus
    ParkResult park;
    Nina::MountReading nina;
    uint64_t nowMs = 0;             // when it was taken (for the N.I.N.A. age)
    uint32_t httpGeneration = 0;    // bumped when that part's settings change
    uint32_t mqttGeneration = 0;
    uint32_t ninaGeneration = 0;
  };
  void writeInputsReport(JsonObject out, const InputsReport &report);
  constexpr size_t INPUTS_REPORT_CAPACITY = 1536;
}
```

The document is in [contracts/roof-inputs.md](contracts/roof-inputs.md).

## Web UI

| Name | Where | What |
|---|---|---|
| `SetupConfig` | `web/src/types/config.ts` | `{ state: 'new' \| 'offered' \| 'done'; step: number }`, optional on `Config` |
| `RoofInputs` | `web/src/types/inputs.ts` | the inputs document |
| `WIZARD_PAGES` | `web/src/components/wizard/pages.ts` | id, title key, settings touched |
| `ReviewCheck` | `web/src/components/wizard/review.ts` | `{ id, state: 'ok' \| 'warn' \| 'bad' \| 'unknown', text, page }` |
| `testResult` | `web/src/components/wizard/inputsTest.ts` | before, after, changed → waiting / result |

## State transitions

```text
no stored settings ──▶ new ──(Finish | Skip confirmed | Dismiss)──▶ done
stored, no setup   ──▶ offered ──(Finish | Skip confirmed | Dismiss)──▶ done
done ──(Run setup wizard)──▶ done (the wizard runs; Finish keeps done)
Demo panel "Restart setup wizard" ──▶ new, step 0
```
