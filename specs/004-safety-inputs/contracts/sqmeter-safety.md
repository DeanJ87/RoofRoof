# Contract consumed: SQMeter safety outputs

Source: SQMeter spec 013 (`contracts/rest-responses.md`, `contracts/mqtt-topics.md`) and SQMeter
spec 016 `contracts/schemas/safety.schema.json`. Details and line references in
[../research.md](../research.md) R1.

## HTTP: `GET <safety.http.url>` (normally `http://<sqmeter>/api/safety`)

RoofRoof requires:

| Field | Required | Type | Use |
|---|---|---|---|
| `safe` | yes | boolean | unsafe when false |
| `reasonFlags` | yes | integer ≥ 0 (uint32) | unsafe when non-zero; bit 9 rain, bit 10 rain-sensor fault |
| `evaluatedAgeMs` | no | integer ≥ 0 | added to the reply's age for the stale check |

Every other field is ignored. HTTP status other than 200, a body that isn't a JSON object, or a
required field missing or of the wrong type: the reply is unreadable (FR-010).

Fixtures: `test/test_safety_input_logic/fixtures.h` (SQMeter's own example, the rain example,
and broken variants).

## MQTT (on RoofRoof's broker), `<base>` = `safety.mqtt.topic` (SQMeter default `sqmeter`)

| Topic | Payloads accepted | Effect |
|---|---|---|
| `<base>/availability` | `online`, `offline` | `offline` → source offline at once; good only after `online` |
| `<base>/safe` | `1`, `0` | a good reply with `reasonFlags` unchanged from the last `safety` (0 if none) |
| `<base>/safety` | the HTTP JSON above | a good reply (flags, rain) |

A message that doesn't match is unreadable. Stale after 150 s without a good `safe` or `safety`.
