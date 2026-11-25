# Quickstart: Crash Capture

## 1. Checks

Everything in `specs/001-platform-bootstrap/quickstart.md` §1, plus:

```bash
~/.platformio/penv/bin/pio test -e native -f test_crash_ring -f test_crash_record -f test_crash_documents
python3 tools/test_decode_crash.py && python3 -m unittest tools/firmware/test_isr_audit.py
```

## 2. The image carries its `.elf`'s SHA-256

```bash
~/.platformio/penv/bin/pio run -e esp32dev
python3 - <<'EOF'
import hashlib
b = open('.pio/build/esp32dev/firmware.bin', 'rb').read()
e = hashlib.sha256(open('.pio/build/esp32dev/firmware.elf', 'rb').read()).digest()
assert b[0x20:0x24] == bytes.fromhex('3254cdab'), 'app descriptor not at 0x20'
assert b[0xb0:0xd0] == e, 'ELF SHA-256 not in the image'
print('ok', e.hex()[:16])
EOF
ls .pio/elf/
```

## 3. Decode a record

```bash
curl -s -u admin:PASSWORD http://roofroof.local/api/diag/crashes > crash.txt
python3 tools/decode_crash.py crash.txt --elf .pio/elf/<build>.elf.gz
# or addresses from a serial monitor:
python3 tools/decode_crash.py --addrs "0x400d5f2c:0x3ffb1230 0x400d61a0:0x3ffb1250" --elf firmware.elf
```

## 4. Bench (pending, owner)

1. Build with `-DRR_TEST_BOOT_CRASH` (the rollback test build calls `abort()` at the end of start-up)
   on a bench board over USB; after the restart (and the rollback), `GET /api/diag/crashes` lists a
   `panic` record whose reason names `abort()`, task `loopTask`, with frames; decode it.
2. Hold a task (a test build that loops in a handler) until the task watchdog fires: a `watchdog`
   record names the task in `watchdogTasks`.
3. Pull the supply briefly to brown out: a `brownout` record, `captured: false`, with the snapshot.
4. `GET /api/status` before and after: `stackFree.loop`, `.roof`, `.asyncTcp` and `minFreeHeap`
   change by less than 200 bytes; the roof's step time in Diagnostics is unchanged.
