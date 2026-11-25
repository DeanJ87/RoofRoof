# Contract: spec 027 REST and Alpaca changes

## POST /api/roof/open, /api/roof/close (password, `X-RoofRoof: 1`)

Body (optional): `{"override": ["weatherNotSafe", "daylight"]}` for Open, `{"override":
["mountNotParked"]}` for Close; only a person's (this route's) overrides count. A body that isn't
that shape, or names anything else, is refused with `400`. A command waiting for the park answers
`202` `{"success": true, "state", "pending": true, "message": "Waiting for the mount to park: ..."}`
(`roof.parkWait` in the status follows it). Refused (`409`) with `reason` and `error` as before: an
override that doesn't cover every overridable refusal present leaves that refusal in place.

## POST /api/nina/test (password)

Body (optional): `{"url": "https://nina.local:1888", "api": "v2"}`; without it the saved address.
`200`: `{"reachable": true, "connected": true, "atPark": false, "slewing": false}`.
`502`: `{"reachable": false, "connected": false, "atPark": false, "slewing": false, "error": "N.I.N.A.
not responding: connection refused"}` (or "N.I.N.A. answered, but: ..." for an unreadable reply or
an API error).
`400` for a bad body or address, `409` while the roof is busy or another read runs.

## Alpaca Dome

- `name` "RoofRoof"; `driverinfo` "RoofRoof v<version>"; `description` "Roll-off roof, shutter only"
  with the bench, demo and test-box prefixes; `/management/v1/configureddevices` DeviceName "RoofRoof".
- `openshutter` / `closeshutter`: success at once when already there, or when the command waits for
  the park; then `shutterstatus` 2/3 and `slewing` true while waiting; 4 after a failed wait until the
  next command.
