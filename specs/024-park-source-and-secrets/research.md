# Research: Park Source and SQMeter Secrets

## R1. Where the park source lives

**Decision**: `park.source` beside `park.switches`; the rule takes a `switchesOnly` flag.
**Rationale**: `park.switches` stays the hardware fact the pins page, the wiring check and the
status already read; the source only says whether N.I.N.A. counts. Every gate reads the one park
result (`SafetyInputs::evaluate` → `RoofControl::setSafety`), so one change covers Close, the
automatic close, the interlock, jog, teach and the calibration run.
**Alternatives**: a fourth `park.switches` value ("two, no N.I.N.A.") mixes count and source; a
boolean `park.ninaConfirms` loses the three named choices the wizard shows.

## R2. Migration

Stored settings and saves without `source` take it from `switches` (none → `nina`, else
`ninaAndSwitches`): exactly the rule before. The web always sends `park.source` with
`park.switches` (a pair in `configPatch.ts`), so a count change never reverts a chosen source.

## R3. Wiring faults

| Fault | Normally open (closes at park) | Normally closed |
|---|---|---|
| Broken wire, loose connector | reads open: not parked (safe) | reads parked (unsafe) |
| Short to ground | reads parked (unsafe) | reads open (safe) |
| Switch stuck closed | reads parked (unsafe) | - |

Breaks are the commoner fault on a moving cable, so normally open stays the only wiring. With
N.I.N.A. in the rule a stuck or shorted switch is caught as a disagreement the first time the mount
unparks. With switches only:

- two switches on separate cables make a single short or stuck switch a disagreement (not parked);
- a switch that never reads open over 3 roof openings is suspect: Open is refused. Normal use
  unparks the mount every imaging night; a cloudy-night open and close without unparking counts,
  so a third such night in a row asks the owner to move the mount once (documented);
- a fresh N.I.N.A. "slewing" vetoes, when an address is set.

A short that appears mid-night with a single switch is not detectable by any of these; it is the
residual risk stated in constitution 5.0.0.

## R4. Counting openings

Counted in the control task when the roof's state becomes Open (not while calibrating), so jog and
Open moves count and a calibration run (which moves the roof several times) doesn't. A switch
reading Open (the debounced contact) clears its count. Saved as two bytes in NVS (`parkWatch`) from
the control task while the roof is still, like the latched fault; at most two writes per night.

## R5. SQMeter's secrets endpoint

Contract (SQMeter, parallel change): `GET /api/secrets` with SQMeter's HTTP Basic login.

| Answer | Meaning | Shown |
|---|---|---|
| 200 `{"mqtt":{"password"},"alerts":{"ntfy":{"token"},"pushover":{"userKey","appToken"}}}` | received ("" = not set) | "password received" |
| 401 | login missing or wrong | "SQMeter refused the login" / "needs its login" |
| 403 `Password sharing is off in SQMeter` | sharing off | "turn it on in SQMeter's Settings" |
| 403 `Set a web password in SQMeter before sharing passwords` | no web password | "set a web password in SQMeter first" |
| 404 | older SQMeter | "Update SQMeter to share passwords" |

`SafetyPoller::fetchJson` parses a 403's body as well as a 200's (filtered to `error`).

## R6. Getting a secret into the settings without the browser

**Decision**: a placeholder value `(from SQMeter)` in the secret field, swapped by
`Config::applyJson` for the held value when given the held secrets, else refused.
**Rationale**: the save already goes through one parse under the save lock with the merged
settings, so the server check sees exactly what will be stored; a restore or any other parse path
gets no held secrets and so can never store the placeholder.
**Alternatives**: a separate "fill from SQMeter" list in the save body needs a second save path in
the web editor; a ticket from the copy has the same plumbing and gains nothing.

## R7. Sync pairing

The secrets read is due in the poller pass after a good settings read (never in the same pass, so
no pass grows past spec 020's bound). The sync uses it only when it was taken after the settings read
it pairs with (`basedOnMs`).

## R8. Flash

Measured after the build; savings sought in the touched modules (shared texts, fewer duplicated
strings).
