# Quickstart: Configurable Policies

## Prerequisites

Spec 001's quickstart §1 tooling; a fetched `origin/main`.

## 1. Defaults are today's behaviour

```bash
~/.platformio/penv/bin/pio test -e native -f test_close_policy
bash tools/close-diff/run.sh 20000   # this branch's close sequence vs origin/main's
```

Expected: all pass; `close-diff: 20000 runs identical`.

## 2. Ranges

```bash
~/.platformio/penv/bin/pio test -e native -f test_config_ranges
(cd web && npx vitest run configRanges)
```

## 3. The UI

```bash
cd web && npm run dev
```

Settings → Roof: the Automatic close card shows four rows and an "Advanced" fold with the rest;
with Alpaca off a note says imaging apps can't open or close the roof. Turn on "Ask before Open
and Close": the Roof card's Open asks once; Stop never does.

## 4. Full check

Spec 001's quickstart §1.
