# Contract: live streams

Both WebSocket endpoints keep their documents unchanged.

| Endpoint | On connect | At rest | Opening or closing | On a change |
|---|---|---|---|---|
| `/ws/status` | the status document | every 2 s | every 500 ms | at once (at most every 250 ms) on a roof state, fault, waiting close, calibration or close-phase change; at once on a settings or recent-alerts change |
| `/ws/diag` | the live diagnostics document (closed at once while diagnostics are off) | every 2 s | every 500 ms | none |

- At most 3 clients per endpoint; a new one past that replaces the oldest. Ping every 15 s; a client
  whose queue stays full for 10 s is closed. (Unchanged.)
- Clients: treat a socket with no message for 5 s as dead; show no reading from a closed connection.
