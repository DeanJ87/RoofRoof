// Spec 029: what a device that crashed twice would report (the firmware's text format,
// lib/CrashLogic/src/CrashDocuments.cpp).
export const mockCrashesText = `This start: panic (4), boot 3
Running: 0.1.0+dev, build 3f2a9c0e11d4b7a2
Records: 2 (the last 3 are kept)

Record 2: panic
Time: 2026-11-25 02:14:07 UTC, 5231 s after start
Reset reason: panic (4)
Firmware: 0.1.0+dev, build 3f2a9c0e11d4b7a2
Reason: LoadProhibited
Task: async_tcp, core 1, stack left 1904 B
Flash write: in progress
Backtrace: 0x400d5f2c 0x400d5f2c 0x400d61a0 0x40089e1d
Memory: free 61224 B, lowest 41020 B, largest block 31732 B
Stack left: loop 5980, roof 3416, async_tcp 6640, safetyPoll 2120, alerts 3876 B
Roof: closed
Log before the restart:
  5229.120s INFO  [Main] Settings saved
  5229.480s INFO  [Main] Settings saved

Record 1: brownout
Time: unknown (clock not set), 812 s after start
Reset reason: brownout (9)
Firmware: 0.1.0+dev, build 3f2a9c0e11d4b7a2
Reason: not captured
`;

export const mockDeviceLogText = `#2 5229.120s INFO  [Main] Settings saved
#3 0.412s INFO  [Main] === RoofRoof Starting ===
#3 0.431s WARN  [Crash] Restarted unexpectedly (panic): crash record 2 kept
`;
