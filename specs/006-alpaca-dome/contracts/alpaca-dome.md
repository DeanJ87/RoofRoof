# Contract: the Alpaca Dome

Base: `/api/v1/dome/0/`. Every reply carries `ClientTransactionID`, `ServerTransactionID`,
`ErrorNumber` and `ErrorMessage` (ASCOM Alpaca). Parameter names are case-insensitive in a GET query
and exact in a PUT form body.

## Management

`/management/v1/configureddevices` while Alpaca is on:

```json
[{"DeviceName": "RoofRoof roof", "DeviceType": "Dome", "DeviceNumber": 0,
  "UniqueID": "roofroof-<12 hex MAC>-dome-0"}]
```

Empty while Alpaca is off.

## Read (GET)

| Member | Value | Error |
|---|---|---|
| `shutterstatus` | 0 open (open magnet), 1 closed (closed magnet), 2 opening, 3 closing, 4 error (also stopped part-way) ([data model](../data-model.md#shutter-state-table)) | 0x407 while Alpaca is off |
| `slewing` | true while opening, closing or a close waits | 0x407 while Alpaca is off |
| `cansetshutter` | true | - |
| `canfindhome`, `canpark`, `cansetaltitude`, `cansetazimuth`, `cansetpark`, `canslave`, `cansyncazimuth` | false | - |
| `slaved` | false | - |
| `altitude`, `azimuth`, `athome`, `atpark` | - | 0x400 NotImplemented |
| `devicestate` | `[{"Name":"ShutterStatus","Value":n},{"Name":"Slewing","Value":b},{"Name":"TimeStamp","Value":"<ISO 8601>"}]` (values only while Alpaca is on; TimeStamp only when the clock is set) | - |
| `interfaceversion` | 3 | - |
| `name` / `description` | `RoofRoof roof` / `Roll-off roof: shutter open, close and stop only` | - |
| common (`connected`, `connecting`, `driverinfo`, `driverversion`, `supportedactions`) | as before | - |

## Act (PUT)

| Member | Effect | Errors |
|---|---|---|
| `openshutter` | the web UI's open, origin client | 0x407 not connected or Alpaca off; 0x40B refused, with the refusal text (also while the roof is closing or a close waits) |
| `closeshutter` | the web UI's close, origin client (a waiting close succeeds) | 0x407; 0x40B refused |
| `abortslew` | stop, position estimate kept; never refused (connected or not, Alpaca on or off); not a person's stop, so the automatic close tries again | - |
| `slaved` (`Slaved=false`) | nothing | `Slaved=true` → 0x400; missing or not a boolean → HTTP 400 |
| `findhome`, `park`, `setpark`, `slewtoaltitude`, `slewtoazimuth`, `synctoazimuth` | nothing | 0x400 |
| `action`, `commandblind`, `commandbool`, `commandstring` | nothing | 0x400 |
| `connected`, `connect`, `disconnect` | the client's connection only; never stops or starts a move | as before |

## Refusal texts (examples)

| Cause | `ErrorMessage` |
|---|---|
| weather not safe | `Weather not safe: <verdict reasons>` |
| mount not parked | `Mount not parked: park not confirmed (N.I.N.A. not responding)` |
| position unknown | `Position unknown: only a person can move the roof` |
| fault latched | `A fault is latched: a person must clear it first: No target magnet after 61.0 s >= 60.0 s timeout` |
| supply low | `Supply 10.9 V < 11.5 V` |
| controller not started | `The roof controller didn't start: the motor stays off` |
| a client's open while closing | `The roof is closing: an imaging app can't reverse a close` |
| upload or restart in progress (open) | `<what is busy>: try again in a moment` |
