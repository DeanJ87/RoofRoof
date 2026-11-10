# Quickstart: Validating the Platform Bootstrap

## Prerequisites

PlatformIO Core 6.2+, Node 24+, Python 3.12, Emscripten (only to rebuild the demo core), an ESP32
DevKit. For SC-002, a logic analyser or multimeter.

Set up once:

```bash
python3 -m venv .venv-quality && .venv-quality/bin/pip install -r tools/quality/requirements.txt
python3 -m venv .venv-docs && .venv-docs/bin/pip install -r docs/requirements.txt
(cd web && npm ci && npx msw init public/) && pio pkg install -e native
```

## 1. Everything CI runs (US5, SC-003)

```bash
pio run -e esp32dev && pio test -e native
(cd web && npm run typecheck && npm run test:coverage && npm run build)
.venv-quality/bin/python tools/quality/check.py
python3 tools/firmware/flash_budget.py --envs esp32dev    # SC-005: < 75% of the slot
(cd web && npm run build:demo && npx playwright test)      # a11y, layout, dashboard, contracts
(cd web && npm run docs:vendor && npm run docs:diagrams) && .venv-docs/bin/mkdocs build --strict
```

Expected: everything passes, `tools/quality/baseline.json` is `{"counts": {}}`, and there are no
compiler warnings.

## 2. Nothing removed is left behind (SC-004)

```bash
git grep -niE "sqmeter|sqm\b|tsl2591|bme280|mlx90614|rg-?15|anemometer|\bwind\b|\bgps\b|bluetooth|\bble\b|oled|battery|esp32-?s3|updates/check|language pack" \
  -- . ':!docs/development/heritage.md' ':!CHANGELOG.md' ':!THIRD_PARTY_NOTICES.md'
```

Expected: no matches, apart from the documented SQMeter references ("based on SQMeter" in the
README and docs, and the source-spec references in spec 001).

## 3. Driver held off (US2, SC-002)

1. Wire the analyser to GPIO25, 26, 27, with 10 kΩ from GPIO27 to GND and no motor.
2. Power-cycle 20 times, reset 5 times, flash over USB 5 times
   (`pio run -t upload && pio run -t uploadfs`), upload firmware on the Updates page 5 times, and
   force 5 crashes.
3. Expected: GPIO27 is never high, and GPIO25/26 are never high together. Once `setup()` starts,
   all three are low.

## 4. New board to network (US1, SC-001)

1. Erase and flash: `pio run -t erase && pio run -t upload && pio run -t uploadfs`.
2. On a phone, join "RoofRoof-Setup". The sign-in page opens Wi-Fi setup; pick the network and
   connect.
3. Expected: within 3 minutes, `http://roofroof.local` shows the dashboard with the Status and
   Device & Network cards.

## 5. Platform behaviour as in SQMeter (US3, US4, SC-006)

On the device:

- Turn on password protection, then call a protected route without credentials → 401.
- Set up ntfy and send a test → delivered, with the result shown per channel.
- Set up MQTT → `roofroof/availability` reads `online`, and Home Assistant shows the device.
- Back up, erase, restore → every setting comes back.
- Upload an SQMeter `firmware.bin` → refused with "Not RoofRoof firmware." before the boot partition is switched; the running firmware is untouched.
- In an Alpaca client (or `curl /management/v1/configureddevices`) → `[]`, with Manufacturer
  "RoofRoof".
