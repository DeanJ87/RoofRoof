# Contributing

See [CONTRIBUTING.md](https://github.com/DeanJ87/RoofRoof/blob/main/CONTRIBUTING.md) in the repo root for the full guide.

---

## Quick Reference

**Coding standard:** see [Coding Standards](coding-standards.md); `python3 tools/quality/check.py` runs what CI checks.

**Firmware build (must be zero warnings):**
```bash
pio run
```

**Docs diagrams:** a change to behaviour shown in a diagram updates the diagram in the same pull request. Then confirm it with `python3 tools/docs/diagrams.py --confirm DIA-NN`. CI flags diagrams whose cited code changed, and fails for the safety-verdict diagrams. See *Diagrams* in CONTRIBUTING.md.

**Unit tests (logic in `lib/`, run on your computer):**
```bash
pio test -e native
cd web && npm test
```

Decision logic lives in `lib/` with a native test suite under `test/`, so it can be tested without hardware:

| Library | What |
|---|---|
| `DriverLogic` | The H-bridge's output levels: never RPWM and LPWM together, `Off` means EN low and both PWM at 0 |
| `AlpacaLogic` | Alpaca API router and discovery (an empty device list for now) |
| `AlertLogic` | Alert engine (imaging-app events), delivery priorities, the send schedule |
| `ConfigModel` | Settings: defaults, JSON, validation |
| `ConfigBackup` | The settings backup file, its encryption and the restore preview |
| `DeviceCore` | Documents and decisions: status, alerts, Alpaca clients |
| `HaDiscovery` | Home Assistant discovery documents |
| `SettingsDeps` | Which settings depend on others |
| `ConnectionBudget`, `NetAddress` | TCP connection limits; address parsing and the IPv6 local-network rule |
| `FirmwareImage` | The firmware marker check for uploads |
| `CaptiveDns` | DNS and the sign-in checks for the setup hotspot |

**The demo's device core.** The live demo runs the code in `lib/` compiled to WebAssembly (`web/src/demo/core/`, committed). After changing anything in `lib/` or `tools/demo-core/`, rebuild it:

```bash
brew install emscripten          # or the emsdk; version in tools/demo-core/VERSION
tools/demo-core/build.sh
cd web && npm run build:demo && npx playwright test tests/demo.spec.ts
```

The browser tests serve the demo on port 4173. To run a second copy alongside (another checkout or worktree), pick another port: `PW_PORT=4196 npx playwright test`.

CI fails with "The demo's device core is out of date" if you forget. Response formats are checked against `specs/001-platform-bootstrap/contracts/schemas/` - by the demo's tests, and on a device with:

```bash
python3 tools/contract-check.py http://<device>
```

If a device response changes on purpose, capture fresh samples and regenerate the schemas with `tools/contracts/generate_schemas.py` (see its header).

**Web UI dev server:**
```bash
cd web
ESP32_IP=<device-ip> npm run dev
```

**Docs preview** (CI builds the docs on every pull request and uploads `site/` as an artifact; there is no public docs site):
```bash
python3 -m venv .venv-docs && .venv-docs/bin/pip install -r docs/requirements.txt
.venv-docs/bin/mkdocs serve
```

Open a PR from a branch — one feature or fix per PR. Keep commit messages short and imperative.
