---
description: "RoofRoof WebSocket API: the device status, with the roof's state, position, readings, checks and automatic-close phase, from /ws/status every 2 seconds (twice a second while the roof moves) and at once when the roof changes."
---

# WebSocket

Connect to `/ws/status` for a live stream of the device status: one message as soon as you connect, then every 2 seconds, and every 500 ms while the roof is opening or closing. The same object is available once from [`GET /api/status`](rest.md#get-apistatus).

---

## Connecting

```javascript
const ws = new WebSocket('ws://roofroof.local/ws/status');

ws.onmessage = (event) => {
  const status = JSON.parse(event.data);
  console.log('Uptime:', status.uptime, 'WiFi:', status.wifi.rssi, 'dBm');
};

ws.onclose = () => {
  // The last message is no longer current: show that, then reconnect with back-off.
};
```

---

## Message Format

Each message is the status document - the same as [`GET /api/status`](rest.md#get-apistatus): firmware, memory, flash partitions, time, WiFi, Ethernet, the pins in effect, MQTT, NTP, alerts, Alpaca, connections and the roof. See the [API Reference](reference.md) for every field. The roof carries `roof.daylight`: the [daylight failsafe](../user-guide/close-on-unsafe.md#daylight-failsafe)'s mode, state, the sun's altitude against the threshold, its text and `closes`, whether daylight wants the roof closed now ([fields](rest.md#get-apistatus)). The roof also carries `roof.testBox`: which [test box](../user-guide/test-box.md) bypasses are in effect, the deadline and a text, always present ([fields](rest.md#get-apistatus)). `location` says where the daylight site came from (SQMeter or typed in), whether it was SQMeter's GPS, when it was saved, the age of the last read and any problem reading it ([Site from SQMeter](../user-guide/close-on-unsafe.md#site-from-sqmeter)). `ethernet` is the wired network on a WT32-ETH01 (link, speed, address; [Network](../user-guide/network.md#ethernet-in-the-status)) and `pins` the board and preset in effect, whether they passed the pin rules and whether a restart is needed ([Pins](../user-guide/pins.md)). `sync` says which settings groups follow SQMeter, when it was last read, any refused value and the secrets to enter ([Keeping in step](../user-guide/close-on-unsafe.md#keeping-in-step)).

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

## Diagnostics stream

With [advanced diagnostics](../user-guide/diagnostics.md) on, `/ws/diag` streams the live diagnostics document: the same document as [`GET /api/diag`](rest.md#get-apidiag).

```javascript
const ws = new WebSocket('ws://roofroof.local/ws/diag');
ws.onmessage = (event) => {
  const diag = JSON.parse(event.data);
  console.log(diag.roof.phase, diag.roof.motorAmps, 'A');
};
```

- One message as soon as you connect, then on the same schedule as `/ws/status`: every 500 ms while the roof is opening or closing, and every 2 s otherwise.
- At most 3 clients are kept (a new one replaces the oldest). A client that stops reading is closed after 10 s.
- With diagnostics off, the device closes a client as soon as it connects.
- Like `/ws/status`, it has no authentication and carries readings only.

---

## Notes

- `/ws/status` is also sent at once when the recent alerts change: `alerts.recentRevision` goes up whenever [`GET /api/alerts/recent`](rest.md#get-apialertsrecent) would answer differently, so a page fetches the list only then.
- `/ws/status` is also sent within 250 ms when the roof's state or fault, a waiting close, or the automatic close's phase changes (`roof` in the status), not only on the regular broadcast. While the roof moves it carries the live position twice a second; MQTT only publishes the roof on a change then.
- Both streams share one schedule (2 s at rest, 500 ms while the roof moves), so the dashboard and the Diagnostics page update at the same rate.
- A client should treat a socket that has sent nothing for 5 s as dead (more than two missed messages), and never show the last message of a closed socket as current. The web UI closes such a socket, says it is reconnecting, and tries again after 1 s, doubling to 30 s; when the page comes back to the foreground it reconnects at once.
- The server broadcasts to all connected clients simultaneously, and limits how many it keeps per endpoint (`connections` in the status).
- While the device is short of memory (many clients at once can fragment it), a message is skipped rather than risk a restart, including the first one after connecting; the next one follows on the usual schedule. `connections.shortOfMemory.skippedFrames` in the status counts them.
- There is no authentication on the WebSocket endpoint.
- The WebSocket at `/ws/sensors` no longer exists.
