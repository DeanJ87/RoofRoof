# Quickstart: Heap-Safe Responses

## Host

```bash
~/.platformio/penv/bin/pio test -e native -f test_heap_safe -f test_device_core -f test_ha_discovery -f test_crash_documents
python3 -m unittest tools/firmware/test_heap_audit.py tools/firmware/test_isr_audit.py
~/.platformio/penv/bin/pio run -e esp32dev
python3 tools/firmware/flash_budget.py --envs esp32dev
```

## Bench (roof closed and at rest, no move commands)

1. `curl -s http://<board>/api/status` → `roof.state` is `closed`, `roof.driving` false.
2. Flash the firmware and LittleFS image over HTTP (`/api/update`, `/api/update/fs`).
3. `python3 tools/load_config_get.py <board> --clients 6 --minutes 5 --also /api/status /api/diag
   "/api/v1/dome/0/shutterstatus?ClientID=7&ClientTransactionID=1"` → passes (no restart).
4. `curl -s http://<board>/api/diag/crashes` → no record newer than the flash.
5. `curl -s http://<board>/api/status | jq .connections.shortOfMemory` → the counters (non-zero is
   fine: requests were refused instead of crashing).
