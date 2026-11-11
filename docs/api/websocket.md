---
description: "RoofRoof WebSocket API: the device status, with the roof's state, position, readings, checks and automatic-close phase, from /ws/status every 2 seconds and at once when the roof changes."
---

# WebSocket

Connect to `/ws/status` for a live stream of the device status, pushed every 2 seconds. The same object is available once from [`GET /api/status`](rest.md#get-apistatus).

---

## Connecting

```javascript
const ws = new WebSocket('ws://roofroof.local/ws/status');

ws.onmessage = (event) => {
  const status = JSON.parse(event.data);
  console.log('Uptime:', status.uptime, 'WiFi:', status.wifi.rssi, 'dBm');
};

ws.onclose = () => {
  // reconnect with backoff
};
```

---

## Message Format

Each message is the status document - the same as [`GET /api/status`](rest.md#get-apistatus): firmware, memory, flash partitions, time, WiFi, MQTT, NTP, alerts, Alpaca, connections and the roof. See the [API Reference](reference.md) for every field. The roof carries `roof.daylight`: the [daylight failsafe](../user-guide/close-on-unsafe.md#daylight-failsafe)'s mode, state, the sun's altitude against the threshold, its text and `closes`, whether daylight wants the roof closed now ([fields](rest.md#get-apistatus)).

!!! note "The same in every language"
    Messages never change with the UI language. Numbers are JSON numbers with `.` as the decimal point and no thousands separators; field names are never translated.

---

## Python Example

```python
import asyncio
import json
import websockets

async def stream():
    async with websockets.connect("ws://roofroof.local/ws/status") as ws:
        async for message in ws:
            status = json.loads(message)
            print(f"v{status['firmware']['version']}  uptime {status['uptime']} s  WiFi {status['wifi']['rssi']} dBm")

asyncio.run(stream())
```

---

## Notes

- `/ws/status` is also sent at once when the recent alerts change: `alerts.recentRevision` goes up whenever [`GET /api/alerts/recent`](rest.md#get-apialertsrecent) would answer differently, so a page fetches the list only then.
- `/ws/status` is also sent within 250 ms when the roof's state or fault, a waiting close, or the automatic close's phase changes (`roof` in the status), not only on the regular 2 s broadcast. While the roof moves this is the live position; MQTT only publishes the roof on a change then.
- The server broadcasts to all connected clients simultaneously, and limits how many it keeps per endpoint (`connections` in the status).
- There is no authentication on the WebSocket endpoint.
- The WebSocket at `/ws/sensors` no longer exists.
