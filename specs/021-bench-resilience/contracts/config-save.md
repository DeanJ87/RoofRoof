# Contract: Settings save with a base

## `GET /api/config`

Unchanged body. New response header:

```text
X-Config-Revision: 1834201977
```

The revision is read before the settings. It changes on every save and starts from a random value
at each start.

## `POST /api/config` (and `PUT`)

The body is a partial settings document, as before. Two optional root keys:

```json
{
  "wifi": { "ipv6": false },
  "baseRevision": 1834201977,
  "baseValues": { "wifi": { "ipv6": true } }
}
```

- No `baseRevision`: saved as before.
- `baseRevision` equals the current revision: saved.
- Otherwise, each leaf of `baseValues` is compared with the device's settings as `GET /api/config`
  shows them (secrets masked; a missing key equals `null`; numbers by value). All equal: saved.
  Any different, or no `baseValues`: refused.

Saved:

```json
{ "success": true, "revision": 1834201978 }
```

Refused (409):

```json
{
  "success": false,
  "error": "Settings changed on the device since this page loaded them: park.nina.url",
  "conflicts": ["park.nina.url"],
  "revision": 1834201980
}
```

Other answers (400 invalid, 409 while the roof moves, 500) are unchanged.
