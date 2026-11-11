# Quickstart: Demo and Bench Mode

## Demo

```bash
cd web && npm run dev:demo      # or: npm run build:demo && npm run preview:demo
```

1. The roof reads closed; open it from the dashboard (or `POST /api/roof/open`); with the Demo
   panel's clock at 10x it opens in about 5 s.
2. Demo panel → Roof → Weather: Unsafe. The device asks the mount to park, waits, closes.
3. Open again, Park fails on, Weather: Rain. The roof stays open; after the park time limit a
   critical "mount didn't park" alert appears.
4. Jam on, then open: the roof stops with a fault; Jam off, Clear fault.

## Bench mode

```bash
~/.platformio/penv/bin/pio run -e esp32dev_bench -t upload   # USB only
~/.platformio/penv/bin/pio run -e esp32dev_bench -t uploadfs
```

On a bare board (GPIO36 tied to ground if no divider is fitted): the banner shows on every page,
the roof opens and closes, EN/RPWM/LPWM stay low (measure them). Alpaca and ConformU work against
the board's Dome.

## Checks

```bash
~/.platformio/penv/bin/pio test -e native -f test_roof_plant -f test_bench_mode -f test_driver_logic -f test_firmware_image
cd web && npx vitest run src/demo
```
