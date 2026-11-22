# Contract: SQMeter site data

## POST /api/sqmeter/site

Fetches SQMeter's `GET /api/config` from the device and answers with what the copy can use.
Password-protected when protection is on (it makes the device send a request).

Request body (optional):

```json
{ "url": "http://192.168.1.50/api/safety" }
```

`url`: any `http://` address of SQMeter (its scheme, host and port are used). Without it, the saved
`safety.httpUrl`.

### 200

```json
{
  "address": "http://192.168.1.50",
  "device": { "name": "SQMeter" },
  "location": { "set": true, "latitude": 51.5074, "longitude": -0.1278 },
  "time": { "timezone": "GMT0BST,M3.5.0/1,M10.5.0", "server1": "pool.ntp.org", "server2": "time.nist.gov" },
  "mqtt": { "broker": "192.168.1.10", "port": 1883, "username": "observatory", "topic": "sqmeter", "passwordSet": true },
  "ntfy": { "server": "https://ntfy.sh", "topic": "my-observatory", "tokenSet": false },
  "pushover": { "userKeySet": true, "appTokenSet": true }
}
```

Every part after `device` may be `null` (an older SQMeter without it). No secret's value ever
appears, masked or not.

### Errors (`{"error": "..."}`)

| Status | When |
|---|---|
| 400 | a body that isn't JSON or is over 512 bytes; no address set, or not `http://` ("SQMeter's address isn't set", "Only http:// is supported") |
| 401 | protection on, wrong or no password |
| 409 | the roof moving, a close waiting, the automatic close working, or another copy running |
| 502 | SQMeter not answering, not 200, or not SQMeter's settings document ("SQMeter not answering: connection refused", "SQMeter answered HTTP 404") |
| 503 | not enough memory to start the fetch |

## GET /api/status, /ws/status: `location`

See [data-model.md](../data-model.md#status-location-section-get-apistatus-wsstatus).

## Settings: `location`

```json
"location": {
  "source": "sqmeter",
  "set": true,
  "latitude": 51.5074,
  "longitude": -0.1278,
  "sqmeter": { "gps": true, "savedAt": 1763540000 }
}
```

`location.sqmeter` is written by the device and ignored on a save. While the source is and stays
`sqmeter`, a save keeps the device's `set`, `latitude` and `longitude`.

## Settings: `sqmeterSync`; status: `sync`

See [data-model.md](../data-model.md#sqmetersyncconfig-libconfigmodel-persisted). While a group
follows and the device has synced it, a save that keeps it following keeps the device's values for
its fields (what was sent is still validated); `follow: false` takes the sent values. `syncedAt` is
never read from a save.
