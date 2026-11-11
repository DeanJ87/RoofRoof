# Contract consumed: N.I.N.A. Advanced API v2

Plugin 2.2.15.0 or newer (2.2.15.3 current on 2026-11-11). Base URL `park.nina.url`, e.g.
`http://192.168.1.20:1888`. No authentication. Sources in [../research.md](../research.md) R2.

## Envelope

`{"Response": <value>, "Error": "<text>", "StatusCode": <n>, "Success": <bool>, "Type": "API"}`

## Mount info: `GET /v2/api/equipment/mount/info`

| Reply | RoofRoof reading |
|---|---|
| HTTP 200, `Success: true`, `Response.Connected` false | Good, not connected |
| HTTP 200, `Success: true`, `Response.Connected` true, `AtPark` and `Slewing` booleans | Good with those values |
| HTTP 200/4xx/5xx, `Success: false` | ApiError, detail = `Error` |
| non-200 without a readable envelope | Unreadable, detail `HTTP <n>` |
| not JSON, `Response` not an object, a needed field missing or not boolean | Unreadable, detail names the field |
| no connection, timeout | Unreachable, detail from the HTTP client |

## Park: `GET /v2/api/equipment/mount/park`

| Reply | Outcome |
|---|---|
| HTTP 200, `Success: true`, `Response: "Parking"` | Accepted (the park runs in N.I.N.A.; confirm with mount info) |
| HTTP 200, `Success: true`, `Response: "Mount already parked"` | AlreadyParked |
| `Success: false` (e.g. 409 "Mount not connected") | Refused, detail = `Error` |
| anything else | Unreadable |

Fixtures: `test/test_nina/fixtures.h`, built from the v2 OpenAPI spec and `Mount.cs` (no live
capture yet; see research "Not verified").

## v3 (not implemented)

`GET /v3/api/equipment/mount` (bare object), `POST /v3/api/equipment/mount/park` → 202, optional
Basic auth. `Nina::Api` carries the method and credentials so a `Nina::ApiV3` can be added.
