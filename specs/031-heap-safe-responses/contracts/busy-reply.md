# Contract: the "busy" reply and the memory counters

## 503 for memory (every route except the essential ones)

```http
HTTP/1.1 503 Service Unavailable
Retry-After: 2
Content-Type: application/json

{"error":"The device is busy: try again"}
```

Sent when the memory guard refuses a request (largest free block < 6 KB or free heap < 24 KB) or
when a reply's buffer can't be allocated. Never sent by the guard to the Alpaca API (`/api/v1/...`,
`/management/...`, `/setup`), a roof command (`POST /api/roof/...`) or an upload (`/api/update`,
`/api/update/fs`); those can still answer 503 if their own reply can't be built.

Clients: retry after the given seconds. The web UI's polls already do.

## `/api/status` → `connections.shortOfMemory`

```json
"connections": {
  "shortOfMemory": { "refused": 0, "skippedFrames": 0 }
}
```

Both count since start. Nothing else in the status document changes.

## WebSocket

A live frame (`/ws/status`, `/ws/diag`) is skipped when the largest free block is below the
frame's length plus 4 KB; `skippedFrames` counts it. A new client's first frame follows the same
rule; the next broadcast brings it up to date.
